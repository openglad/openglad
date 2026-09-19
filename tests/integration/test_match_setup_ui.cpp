/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The SETUP wizard in real SDL (docs/match-setup-design.md §2): the door
// twin on the Base Camp strip, the five steps, the tab jumps, the reverse
// cell and the right-click, GO's gated face, and the #305 census a fresh
// ball arena deals with no knob touched.
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
#include <openglad/interface/button.h>
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

#include <SDL3/SDL.h>

#include <atomic>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

void picker_main(Sint32 argc, char** argv);
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
    bool closed_from_every_step = true;
    bool difficulty_hidden = false;
    bool setup_visible = false;
    int captures = 0;
};

// The shared opening: main menu -> Base Camp -> the strip's SETUP door.
// Returns 0 on success, or the leg the escape tail gave up on.
int enter_wizard(SetupFlowState* state, int leg_base,
                 const std::function<int(int, const char*)>& escape)
{
    state->camp_seen = wait_for_interactable("continue_game", 10000);
    if (!state->camp_seen)
        return escape(leg_base, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");

    state->door_seen = wait_for_interactable("setup", 15000);
    if (!state->door_seen)
        return escape(leg_base + 1, "the versus strip never showed SETUP");
    state->setup_visible = true;
    state->difficulty_hidden = !has_interactable("difficulty");

    state->wizard_opened = open_setup_step(0, "GAME", 15000);
    if (!state->wizard_opened)
        return escape(leg_base + 2, "the SETUP door never opened the wizard");
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// 1. The door twin, and the strip's nav around it.
//    On a versus campaign the strip's second door is SETUP and the
//    DIFFICULTY door is not there at all — one door per campaign kind (D9).

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

TEST(MatchSetupUi, door_twin_visibility_and_strip_nav)
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
    EXPECT_TRUE(state.setup_visible)
        << "a versus campaign's strip carries the SETUP door";
    EXPECT_TRUE(state.difficulty_hidden)
        << "and not DIFFICULTY: the fight's rules are the RULES step";
    EXPECT_TRUE(state.wizard_opened);
    EXPECT_NE(std::string::npos, state.game_row.find("TEAM DEATHMATCH"))
        << "the GAME step is the campaign's own book root: '"
        << state.game_row << "'";
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
    state->arena_on_current_window =
        state->arena_row.find("[CURRENT]") != std::string::npos;
    capture_presented_frame("setup_step_arena", uxshots_dir());
    ++state->captures;

    // TEAMS: the swatched team lines, the campaign's own line, the FILL
    // wheel with its reverse cell, and the LINEUP door.
    state->reached_teams = open_setup_step(2, "TEAMS", 15000);
    if (!state->reached_teams)
        return escape(5, "the TEAMS tab never came up");
    (void)wait_for_menu_frames(2);
    state->teams_fill_row = interactable_label("setup_row_0");
    capture_presented_frame("setup_step_teams", uxshots_dir());
    ++state->captures;

    // RULES: nine cycler rows, each with its own "<" cell.
    state->reached_rules = open_setup_step(3, "RULES", 15000);
    if (!state->reached_rules)
        return escape(6, "the RULES tab never came up");
    (void)wait_for_menu_frames(2);
    state->rules_row_0 = interactable_label("setup_row_0");
    state->rules_row_1 = interactable_label("setup_row_1");
    capture_presented_frame("setup_step_rules", uxshots_dir());
    ++state->captures;

    // MATCH: the staged census, the rules recap, VIEW LEVEL over GO.
    state->reached_match = open_setup_step(4, "MATCH", 15000);
    if (!state->reached_match)
        return escape(7, "the MATCH tab never came up");
    (void)wait_for_menu_frames(2);
    state->go_row = interactable_label("setup_row_1");
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
    write_versus_save("modes", 820);

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
    EXPECT_TRUE(state.reached_match);
    EXPECT_NE(std::string::npos, state.go_row.find("GO"))
        << "the MATCH step ends on GO: '" << state.go_row << "'";
    EXPECT_TRUE(state.walked_back)
        << "the tabs jump back down the strip";
    verify_captured_frames("setup_walk", 4);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 3. The reverse step, both ways into ONE session entry (D19): the row's
//    "<" cell and a right-click on the row itself must land on the same
//    face, and neither may be reachable by LEFT/RIGHT (those stay nav).

namespace
{
struct ReverseState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool stepped_forward = false;
    bool cell_stepped_back = false;
    bool right_click_stepped_back = false;
    bool finished = false;
};

int reverse_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<ReverseState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("setup", 15000))
        return escape(2, "the versus strip never showed SETUP");

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

    // The "<" cell steps the SAME wheel back: 1 -> MAP. Every pointer and
    // every pad can reach it, which the right-click alone could not. The
    // cell's OWN face never moves (it is the glyph "<"), so the edge this
    // ladder watches is the ROW's face — press one id, watch another.
    state->cell_stepped_back = click_until_edge(
        "setup_rev_0",
        [](int wait_ms) {
            return wait_for_interactable_label_matching(
                "setup_row_0",
                [](const std::string& label) {
                    return label.find("SCORE: MAP") != std::string::npos;
                },
                wait_ms);
        },
        "turned", 3, 10000, "setup");

    // And a right-click on the row itself is the third way in. It is not a
    // ladder (interact_right sends one press), so prove it by the face.
    if (state->cell_stepped_back) {
        (void)click_until_label_containing("setup_row_0", "SCORE: 1", 3,
                                           10000, "turned", "setup");
        (void)interact_right("setup_row_0");
        state->right_click_stepped_back =
            wait_for_interactable_label_matching(
                "setup_row_0",
                [](const std::string& label) {
                    return label.find("SCORE: MAP") != std::string::npos;
                },
                10000);
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

TEST(MatchSetupUi, reverse_cell_and_right_click_step_a_rules_cycler_back)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 300);

    ReverseState state;
    SDL_Thread* thread =
        SDL_CreateThread(reverse_injector, "setup_reverse", &state);
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
    EXPECT_TRUE(state.cell_stepped_back)
        << "the row's '<' cell steps the SAME wheel back to MAP — a "
           "forward-only wheel costs a full lap on an overshoot, and the "
           "cell is the reverse every pointer and every pad can reach";
    EXPECT_TRUE(state.right_click_stepped_back)
        << "and a right-click on the row is the third way into the one "
           "session entry (D19)";
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
    if (!wait_for_interactable("setup", 15000))
        return escape(2, "the versus strip never showed SETUP");

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
    if (!wait_for_interactable("setup", 15000))
        return escape(2, "the versus strip never showed SETUP");

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
    if (!wait_for_interactable("setup", 15000))
        return escape(2, "the versus strip never showed SETUP");

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
    if (!wait_for_interactable("setup", 15000))
        return escape(2, "the versus strip never showed SETUP");

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
// 8. The two doors, and the right-click that reaches them. TEAMS carries
//    the LINEUP door and MATCH the VIEW LEVEL door; both open a NESTED
//    engine screen over the wizard and both come back to the step that
//    opened them.
//
//    The right-click is the part no flow covered and the runtime's own
//    invariant caught nothing about: a right-click on a door row arrives as
//    choose(row, -1), which is still the door, and the reverse flag it set
//    used to be cleared only AFTER the dispatch returned — i.e. after the
//    nested screen's whole loop had run. Its first frame therefore opened
//    holding a flag from another screen's click: under TESTING the runner
//    aborts on it, and in production the nested screen's first row press
//    would have stepped its wheel backwards.

namespace
{
struct DoorsState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool lineup_opened_by_right_click = false;
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
    if (!wait_for_interactable("setup", 15000))
        return escape(2, "the versus strip never showed SETUP");

    state->opened = open_setup_step(2, "TEAMS", 15000);
    if (!state->opened)
        return escape(3, "the TEAMS step never came up");
    (void)wait_for_menu_frames(2);

    // A RIGHT-click on the LINEUP door. interact_right sends ONE press, so
    // the oracle is the nested screen's own id.
    (void)interact_right("setup_row_1");
    state->lineup_opened_by_right_click =
        wait_for_interactable("lineup_unite", 15000);
    if (!state->lineup_opened_by_right_click)
        return escape(4, "the LINEUP door did not open on a right-click");
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
    EXPECT_TRUE(state.lineup_opened_by_right_click)
        << "D19: a right-click on a DOOR row is still the door — and the "
           "reverse flag it set must not survive into the nested screen "
           "(under TESTING the runner aborts the binary on one that does)";
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
    if (!wait_for_interactable("setup", 15000))
        return escape(2, "the versus strip never showed SETUP");

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
class JoinerLobbyClient final : public og::ui::IPickerLobbyClient
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
    bool no_reverse_cells = false;
    bool arena_refused = false;
    bool still_on_arena = false;
    std::string match_last_row;
    bool finished = false;
    int captures = 0;
};

int joiner_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<JoinerState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("setup", 15000))
        return escape(1, "the versus strip never showed SETUP");

    state->opened_rules = open_setup_step(3, "RULES", 15000);
    if (!state->opened_rules)
        return escape(2, "the RULES step never came up");
    (void)wait_for_menu_frames(2);
    // A read-only row carries no reverse cell: the cells are the host's
    // half of the wheel, and a joiner has no wheel.
    state->no_reverse_cells = !has_interactable("setup_rev_0") &&
        !has_interactable("setup_rev_1");
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

    if (!open_setup_step(4, "MATCH", 15000))
        return escape(4, "the MATCH step never came up");
    (void)wait_for_menu_frames(2);
    state->match_last_row = interactable_label("setup_row_1");

    // A joiner's strip wears READY where the host's wears GO, so the
    // camp's own door is the edge that says the wizard closed.
    if (!click_until_edge("setup_back", [](int wait_ms) {
            return wait_for_interactable("setup", wait_ms);
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
    EXPECT_TRUE(state.no_reverse_cells)
        << "a read-only row has no '<' cell";
    EXPECT_TRUE(state.arena_refused)
        << "a joiner's level row refuses in WORDS — it is never hidden, "
           "because a joiner has to be able to read which arenas exist";
    EXPECT_TRUE(trace_contains(
        "setup", std::string(og::ui::kCampaignPickerHostGuardMessage).c_str()))
        << "and the refusal is the engine's one sentence for it";
    EXPECT_TRUE(state.still_on_arena) << "a refusal never advances";
    EXPECT_EQ(std::string(og::ui::kSetupJoinerReadyRow),
              state.match_last_row)
        << "the joiner's GO slot holds the pointer row: READY is the Base "
           "Camp strip's, and the wizard says where — '"
        << state.match_last_row << "'";
    verify_captured_frames("setup_joiner", 1);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 11. A paged ARENA window. CTF ships ten arenas and the step shows seven,
//     so the pagers un-park beside the first row and the window can be
//     stepped. The step opens on the window holding [CURRENT] (D27) — a
//     host who sees four green rows none of which is theirs cannot tell
//     what GO would launch.

namespace
{
struct PagerState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    bool pagers_shown = false;
    bool current_on_entry = false;
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
    if (!wait_for_interactable("setup", 15000))
        return escape(2, "the versus strip never showed SETUP");

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
    // The window the step opened on holds the cursor's arena.
    for (int r = 0; r < og::ui::kSetupRowsMax; ++r) {
        const std::string label =
            interactable_label("setup_row_" + std::to_string(r));
        if (label.find("[CURRENT]") != std::string::npos)
            state->current_on_entry = true;
    }

    // A page step moves a window, not a setting: nothing autosaves.
    state->stepped = click_and_acknowledge_trace(
        "setup_page_next", "setup", "page 2/2",
        /*waits_for_autosave=*/false, 10000);
    state->stepped_back = click_and_acknowledge_trace(
        "setup_page_prev", "setup", "page 1/2",
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
    // CTF: ten arenas, 507 sits on the second window.
    write_versus_save("modes", 507);

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
        << "ten arenas over a nine-row floor: the pagers un-park";
    EXPECT_TRUE(state.current_on_entry)
        << "D27: the step opens on the window that holds [CURRENT]";
    EXPECT_TRUE(state.stepped) << "'>' steps the window to 2/2";
    EXPECT_TRUE(state.stepped_back) << "and '<' steps it back";
    verify_captured_frames("setup_pager", 1);
    restore_gladiator_mount();
}

// ---------------------------------------------------------------------------
// 12. DIFFICULTY is a session value, not a save field, and the RULES row
//     writes it through the SAME value-taking tail the Base Camp's own
//     DIFFICULTY door calls (ruling 2). The click's answer must be on the
//     face the NEXT frame composes: refetching with the Inputs the dispatch
//     was handed re-reads the OLD session difficulty and re-composes the
//     face the player just replaced, and only the next restage — a quarter
//     of a second later — heals it.

namespace
{
struct DifficultyRowState
{
    std::atomic<bool> test_finished{false};
    bool opened = false;
    std::string before;
    std::string after_one_frame;
    bool finished = false;
};

int difficulty_row_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<DifficultyRowState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kSetupEscapeDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    if (!wait_for_interactable("setup", 15000))
        return escape(2, "the versus strip never showed SETUP");

    state->opened = open_setup_step(3, "RULES", 15000);
    if (!state->opened)
        return escape(3, "the RULES step never came up");
    (void)wait_for_menu_frames(2);
    state->before = interactable_label("setup_row_6");

    // One click, acknowledged by the write's own trace, then exactly ONE
    // completed frame: no settle, because a face that needs a settle is a
    // face that was wrong when the player let go of the button.
    if (!click_and_acknowledge_trace("setup_row_6", "setup", "difficulty",
                                     /*waits_for_autosave=*/false, 10000))
    {
        return escape(4, "the DIFFICULTY row never took the click");
    }
    (void)wait_for_menu_frames(1);
    state->after_one_frame = interactable_label("setup_row_6");

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

TEST(MatchSetupUi, difficulty_row_answers_on_the_very_next_frame)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    write_versus_save("modes", 820);
    const int saved_difficulty =
        og::runtime::current_session->current_difficulty_;
    og::runtime::current_session->current_difficulty_ = 0;

    DifficultyRowState state;
    SDL_Thread* thread =
        SDL_CreateThread(difficulty_row_injector, "setup_difficulty", &state);
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
    // match_upper is picker_common's own (file-static): the faces are
    // og::ui::format_difficulty_label upper-cased, spelled here so a
    // relabel of either half fails loudly.
    EXPECT_EQ("DIFFICULTY: SKIRMISH - up to slaughter", state.before)
        << "'" << state.before << "'";
    EXPECT_EQ("DIFFICULTY: BATTLE - up to slaughter", state.after_one_frame)
        << "the click's answer is on the NEXT frame's face, not a restage "
           "later: '" << state.after_one_frame << "'";
    EXPECT_EQ(1, og::runtime::current_session->current_difficulty_)
        << "and the value the session holds is the one the row wrote";

    og::runtime::current_session->current_difficulty_ = saved_difficulty;
    restore_gladiator_mount();
}
