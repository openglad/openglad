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
#include "test_click_ladder.h"
#include "test_escape_tail.h"
#include "test_frame_capture.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

void picker_main(Sint32 argc, char** argv);
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

// A versus save the wizard can be opened over: two deployed soldiers on
// team 0, a defined resting state for every knob the macro faces derive
// from, and no arena deal memo left by an earlier flow.
void write_versus_save(const std::string& campaign, short scen_num)
{
    SaveData& save = test_screen()->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    const char* names[] = {"Alpha", "Beta"};
    for (std::size_t i = 0; i < 2; ++i) {
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[i]->name = names[i];
        save.team_list[i]->teamnum = 0;
        save.team_list[i]->deployed = true;
        save.team_list[i]->campaign_tag = 0;
    }
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;
    save.fill = {};
    save.map_units = {};
    save.arena_lineup_dealt_campaign.clear();
    save.arena_lineup_dealt_scen = 0;
    save.ctf_capture_limit = 0;
    save.time_limit = 0;
    save.scen_num = scen_num;
    save.current_campaign = campaign;
    save.current_levels.clear();
    save.current_levels[campaign] = scen_num;
    save.m_totalcash[0] = 5000;
    save.campaign_state.clear();
    save.completed_levels.clear();
    save.save_name = "IRON KETTLE";
    ASSERT_TRUE(save.save("save0"));
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

    // ...and back down the strip with PREV, which is what makes the footer
    // a wizard rather than two unrelated buttons.
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
        << "PREV/the tabs walk back down the strip";
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
