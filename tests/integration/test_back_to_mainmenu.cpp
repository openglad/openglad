#include <memory>
#include <array>
#include <atomic>
#include <string>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_company_cleanup.h"
#include "test_interact.h"
#include "test_click_ladder.h"
#include "test_escape_tail.h"
#include <openglad/resources/save_data.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
void picker_mainmenu_loop();
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


// Test: Continue -> Back should return to the main menu, not exit.
//
// Before the fix, picker_main called mainmenu() once with no loop.
// When create_team_menu returned EXIT (from the BACK button), mainmenu
// also returned EXIT, picker_main fell through, and the program exited.
// run_picker's TeamBuild case carries the rule now: a team-build screen that
// did not start a game transitions back to PickerScreen::MainMenu
// (src/interface/ui/picker_state.cpp).
//
// This test drives two full iterations of the main menu loop:
//   Iteration 1: mainmenu -> click CONTINUE -> create_team_menu -> click BACK
//   Iteration 2: mainmenu reappears -> click CONTINUE -> click BACK again
//
// A background thread uses the interaction API (wait_for_interactable,
// wait_for_menu_frames, the shared click ladder) to drive navigation while the
// main thread runs the blocking menu event loops -- same pattern as
// test_level_progress_menu.
//
// Every leg of that flow is NUMBERED and every give-up routes through the
// escape tail below instead of returning into a wedged binary: picker_main and
// picker_mainmenu_loop block on the MAIN thread and only a click can make them
// return, so an injector that gave up silently took the whole og_test_menu_ui
// binary to the 420 s CTest ceiling. BackToMainmenu.a_leg_that_gives_up_frees_
// the_main_thread is the regression for that.

namespace {
// Cancellation ceilings for a pump that has stopped, never budgets. Do not
// raise them to buy time on an instrumented lane: a lane that needs more than
// these has stopped pumping, and the escape tail is what turns that into a
// named red instead of a wedged binary.
constexpr int kMainMenuWaitMs = 5000;
// CONTINUE loads the company and its campaign level on the menu thread, so its
// door gets the ceiling the flat-sleep version used to spend on it.
constexpr int kTeamDoorWaitMs = 10000;
// BACK is a plain menu swap with nothing to load.
constexpr int kMainMenuBackWaitMs = 5000;
// The sabotaged leg waits for an id nothing ever publishes. Its window is
// short on purpose: nothing is coming, and the point of that run is the tail.
constexpr int kSabotageWaitMs = 500;

// escape_to_the_main_thread, kEscapePollMs and kEscapeConfirmTicks -- the
// give-up rule this file's legs route through -- now live in
// tests/test_escape_tail.h, so the capture flows share one implementation of
// it (PR #245's no-rule-twins principle). The ruling comments moved with the
// code; only the leg ceilings above stayed here, because they are this
// file's budgets rather than the rule.
}  // namespace

struct BackTestState {
    bool started = false;
    bool finished = false;
    int times_saw_mainmenu = 0;  // how many times "continue_game" appeared
    // Set by the MAIN thread after picker_main returns, never by the injector.
    std::atomic<bool> test_finished{false};
    // Point one leg at an id the flow never publishes, so the give-up path
    // itself is testable.
    int sabotage_leg = 0;
};

struct LoopTestState {
    bool activated = false;
    std::atomic<bool> test_finished{false};
};

static bool wait_for_base_camp(int wait_ms)
{
    return wait_for_interactable("go", wait_ms);
}

static bool wait_for_main_menu(int wait_ms)
{
    return wait_for_interactable("continue_game", wait_ms);
}

static int back_test_injector(void* data)
{
    og::runtime::ensure_thread_session();
    BackTestState* state = static_cast<BackTestState*>(data);
    state->started = true;

    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why);
    };

    // === Iteration 1 ===
    // -- Leg 1: the main menu is up --
    const char* const start_id =
        state->sabotage_leg == 1 ? "never_published_door" : "continue_game";
    const int start_wait_ms =
        state->sabotage_leg == 1 ? kSabotageWaitMs : kMainMenuWaitMs;
    if (!wait_for_interactable(start_id, start_wait_ms) ||
        !wait_for_menu_frames(2))
        return escape(1, "the main menu never published continue_game");
    state->times_saw_mainmenu++;

    // -- Leg 2: CONTINUE opens base camp --
    fprintf(stderr, "  [test] Iteration 1: clicking continue_game\n");
    if (!click_until_edge("continue_game", wait_for_base_camp,
                          /*landed_trace=*/nullptr, /*attempts=*/3,
                          /*wait_ms=*/kTeamDoorWaitMs) ||
        !wait_for_menu_frames(2))
        return escape(2, "CONTINUE never opened the team-build screen");

    // -- Leg 3: BACK re-enters the main menu (THE RULE UNDER TEST) --
    // Before the fix: picker_main returned here and the program exited, so
    // the edge this ladder waits on -- the main menu's CONTINUE door -- never
    // came back. The edge IS the assertion; times_saw_mainmenu records it.
    fprintf(stderr, "  [test] Iteration 1: clicking back\n");
    if (!click_until_edge("back", wait_for_main_menu,
                          /*landed_trace=*/nullptr, /*attempts=*/3,
                          /*wait_ms=*/kMainMenuBackWaitMs) ||
        !wait_for_menu_frames(2))
        return escape(3, "the main menu did not reappear after BACK");
    state->times_saw_mainmenu++;

    // === Iteration 2: the same door, on the menu the loop re-entered ===
    fprintf(stderr, "  [test] Iteration 2: clicking continue_game\n");
    if (!click_until_edge("continue_game", wait_for_base_camp,
                          /*landed_trace=*/nullptr, /*attempts=*/3,
                          /*wait_ms=*/kTeamDoorWaitMs) ||
        !wait_for_menu_frames(2))
        return escape(4,
                      "CONTINUE never re-opened the team-build screen on the "
                      "second iteration");

    state->finished = true;
    // Iteration 2's BACK is the tail's: picker_main returns under it.
    fprintf(stderr, "  [test] Iteration 2: leaving through the escape tail\n");
    return escape(0, "");
}

static void cleanup_picker_state()
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

static int single_mainmenu_loop_injector(void* data)
{
    og::runtime::ensure_thread_session();
    LoopTestState* state = static_cast<LoopTestState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why);
    };

    if (!wait_for_interactable("continue_game", kMainMenuWaitMs) ||
        !wait_for_menu_frames(2))
        return escape(1,
                      "the legacy loop's main menu never published "
                      "continue_game");

    if (!click_until_edge(
            "continue_game",
            [](int wait_ms) {
                return wait_for_interactable("hire_troops", wait_ms) &&
                       wait_for_interactable("back", wait_ms);
            },
            /*landed_trace=*/nullptr, /*attempts=*/3,
            /*wait_ms=*/kTeamDoorWaitMs) ||
        !wait_for_menu_frames(2))
        return escape(2,
                      "CONTINUE never opened the team-build screen from the "
                      "legacy loop");

    state->activated = true;
    // The BACK that ends the legacy pass belongs to the tail, for the same
    // reason iteration 2's does: picker_mainmenu_loop returns under it.
    return escape(0, "");
}

// The company CONTINUE must open. A bare SaveData::save() never stamps
// last_played_unix_s, so a company another test founded would take the session
// over silently; seed through the autosave choke point that stamps.
static void seed_continue_company()
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(seed_open_company(save, "save0", newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";
}

TEST(BackToMainmenu, continue_then_back_returns_to_mainmenu) {
    trace_clear();

    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";
    ASSERT_NO_FATAL_FAILURE(seed_continue_company());

    // Start the event injector thread
    BackTestState state;
    SDL_Thread* thread = SDL_CreateThread(back_test_injector, "back_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create event injector thread";

    // Allow 2 mainmenu iterations then exit the loop
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;

    // This blocks in picker_main's menu loop -- the injector thread drives it
    picker_main(0, nullptr);

    // The tail is running until it is told the menus are gone for good.
    state.test_finished.store(true);
    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();

    // Exercise the legacy loop entry itself against a live, initialized
    // picker.  A natural CONTINUE -> team-build BACK round trip must complete
    // one real legacy menu pass; the TESTING iteration cap then returns
    // control without terminating the process.
    LoopTestState loop_state;
    SDL_Thread* loop_thread = SDL_CreateThread(
        single_mainmenu_loop_injector, "single_mainmenu_loop", &loop_state);
    ASSERT_TRUE(loop_thread != nullptr);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_mainmenu_loop();
    loop_state.test_finished.store(true);
    int loop_thread_result = -1;
    SDL_WaitThread(loop_thread, &loop_thread_result);
    escape_tail_join_hygiene();
    const int mainmenu_calls_after_loop = g_picker_mainmenu_calls;

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, loop_thread_result)
        << "the legacy-loop injector gave up at leg " << loop_thread_result;
    EXPECT_TRUE(loop_state.activated)
        << "the legacy loop must have opened base camp through CONTINUE";
    EXPECT_EQ(1, mainmenu_calls_after_loop)
        << "picker_mainmenu_loop must run exactly one capped iteration";

    ASSERT_EQ("save0", og::data::active_company_slot())
        << "the flow must have run on the company this test seeded";

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;

    // The real assertion, and the first one to read: the injector saw the main
    // menu appear TWICE, which is the maximum this flow can reach -- once on
    // entry and once more because BACK from base camp re-entered it. Before
    // the fix (no loop) mainmenu only appeared once, and breaking run_picker's
    // TeamBuild -> MainMenu transition today reproduces exactly that 1.
    EXPECT_EQ(2, state.times_saw_mainmenu)
        << "main menu should reappear after Continue->Back (not exit the "
           "program)";
    EXPECT_TRUE(state.finished) << "injector thread should have completed";
}

// The regression for the wedge itself: a leg that gives up while the main
// thread is blocked inside picker_main must free that thread and report its
// number. Leg 1 is pointed at an id the main menu never publishes; the escape
// tail then has to walk CONTINUE -> base camp -> BACK, so picker_main returns
// and SDL_WaitThread joins.
//
// Replace `return escape(1, ...)` with a bare `return 1` (the shape this file
// carried before PR #292) and this test hangs until the CTest ceiling.
TEST(BackToMainmenu, a_leg_that_gives_up_frees_the_main_thread) {
    trace_clear();

    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";
    ASSERT_NO_FATAL_FAILURE(seed_continue_company());

    BackTestState state;
    state.sabotage_leg = 1;
    SDL_Thread* thread =
        SDL_CreateThread(back_test_injector, "back_escape_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create event injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    state.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(thread, &injector_result);
    escape_tail_join_hygiene();

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(state.started) << "the injector thread never ran";
    EXPECT_EQ(1, injector_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_EQ(0, state.times_saw_mainmenu)
        << "leg 1 gave up before it ever saw the main menu it was pointed at";
    EXPECT_FALSE(state.finished)
        << "a flow that gave up at leg 1 never completed";
}

// --- The highlighted-button mirror ----------------------------------------
//
// run_menu_screen keeps its keyboard highlight in a LOCAL (`highlighted_button`,
// menu_screen_runner.cpp) with no accessor, so an injector could prove a nav
// key was consumed only through the pulsing highlight ring -- which
// draw_highlight animates off wall-clock ticks and paints in the same YELLOW
// as the hover box, i.e. an oracle that depends on the frame's phase.
// menu_screen_testing_highlighted_button() is the TESTING mirror of that
// local: published immediately BEFORE each completed-frames bump, and reset
// to -1 by an RAII scope so every exit path of run_menu_screen (six of them,
// two nested-door propagations included) leaves it cleared.
//
// This test pins BOTH halves on the main menu, whose spec fixes the entry
// highlight at row 1 = continue_game (menu_screen_specs.cpp,
// `spec.default_highlight = 1;  // continue_game`). The nav-walk teeth -- the
// mirror MOVING with a nav key -- belong to the nav_step helper, not here.
struct HighlightMirrorState {
    std::atomic<bool> test_finished{false};
    // -99 = the injector never got to read the mirror at all, which no
    // passing run may leave behind.
    int mirror_index = -99;
    std::string mirror_id = "<unread>";
};

static int highlight_mirror_injector(void* data)
{
    og::runtime::ensure_thread_session();
    HighlightMirrorState* state = static_cast<HighlightMirrorState*>(data);
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why);
    };

    if (!wait_for_interactable("continue_game", kMainMenuWaitMs) ||
        !wait_for_menu_frames(2))
        return escape(1, "the main menu never published continue_game");

    // Read the index and the array it indexes in ONE menu-thread task: a
    // split read could pair this frame's index with the next screen's
    // buttons.
    if (!run_on_main_thread([state] {
            state->mirror_index = og::ui::menu_screen_testing_highlighted_button();
            state->mirror_id = "<no button at that index>";
            AllButtonsLock lock;
            if (state->mirror_index >= 0 && state->mirror_index < MAX_BUTTONS) {
                const vbutton* row =
                    og::runtime::current_session
                        ->allbuttons_[static_cast<std::size_t>(
                            state->mirror_index)];
                if (row != nullptr)
                    state->mirror_id = row->id;
            }
        }))
        return escape(2, "the menu loop never read the highlight mirror");

    return escape(0, "");
}

TEST(MenuScreenHighlightMirror, publishes_the_live_highlight_and_clears_on_exit) {
    trace_clear();

    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";
    ASSERT_NO_FATAL_FAILURE(seed_continue_company());

    ASSERT_EQ(-1, og::ui::menu_screen_testing_highlighted_button())
        << "the mirror must read -1 while no engine menu screen is running";

    HighlightMirrorState state;
    SDL_Thread* thread =
        SDL_CreateThread(highlight_mirror_injector, "highlight_mirror", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create event injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    state.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(thread, &injector_result);
    escape_tail_join_hygiene();

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, injector_result)
        << "the injector gave up at leg " << injector_result;
    EXPECT_EQ(1, state.mirror_index)
        << "the live main menu must publish its entry highlight, row 1 "
           "(menu_screen_specs.cpp: spec.default_highlight = 1)";
    EXPECT_EQ("continue_game", state.mirror_id)
        << "row 1 of the main menu is CONTINUE; the mirror must index the "
           "live button array the injector clicks through";
    EXPECT_EQ(-1, og::ui::menu_screen_testing_highlighted_button())
        << "run_menu_screen must clear the mirror on its way out, so a "
           "reader after a screen closed cannot see the last screen's index";
}
