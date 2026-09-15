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
// Poll ticks, not settles. Every screen transition in this file settles on
// wait_for_menu_frames(2) -- the main menu and base camp are both
// run_menu_screen-hosted (menu_screen_specs.cpp) -- and every click is proven
// consumed by the screen it opens. These are the ticks of wait-on-condition
// loops, which is why they are spelled with "poll"
// (scripts/check_injector_settles.sh, tier 2).
constexpr Uint32 kEscapePollMs = 100;
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
// One press per screen, not one per tick: after each press the tail watches
// for the screen it just acted on to go away, and only then presses again.
// A blind re-press is a RAW COORDINATE click on whatever came up in the
// meantime -- base camp's roster band sits where CONTINUE was -- which is how
// an escape tail opens a submenu it does not know how to leave. Bounded, so a
// press that evaporated is still re-sent.
constexpr int kEscapeConfirmTicks = 20;

// The escape tail, shared by both injectors (the shape of
// tests/integration/test_pause_menu.cpp and test_overpowered_team.cpp): keep
// closing whatever screen is currently published until the MAIN thread says it
// is out of the blocking menu for good, and report the leg that gave up.
//
// It has no wall-clock bound on purpose. The main thread cannot leave
// picker_main / picker_mainmenu_loop on its own, so a tail that stopped trying
// early would GUARANTEE the wedge it exists to prevent.
//
// The happy path ends in this same loop (escape(0)), so the FINAL back of
// every flow is driven by one exit rule and never by click_until_edge: after
// picker_main returns there is no pump left to service a ladder's acknowledge
// post, and the ladder would spend 3 x kAckPostCeilingMs there.
int escape_to_the_main_thread(std::atomic<bool>& test_finished, int leg,
                              const char* why)
{
    if (leg != 0)
        fprintf(stderr, "  [test] ERROR: leg %d: %s\n", leg, why);
    const auto press_and_watch = [&test_finished](const std::string& click_id,
                                                  const std::string& watched) {
        (void)interact(click_id);
        for (int tick = 0; tick < kEscapeConfirmTicks &&
                           !test_finished.load() && has_interactable(watched);
             ++tick)
            SDL_Delay(kEscapePollMs);
    };
    while (!test_finished.load()) {
        // "Closing" the main menu means going FORWARD through it: mainmenu()
        // blocks until the player picks something, and with the iteration cap
        // reached the next visit is answered with QUIT without drawing, so
        // CONTINUE -> base camp -> BACK is the way OUT.
        if (has_interactable("go"))
            press_and_watch("back", "go");
        else if (has_interactable("continue_game"))
            press_and_watch("continue_game", "continue_game");
        else
            SDL_Delay(kEscapePollMs);
    }
    return leg;
}
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

    ScopedCompanyFileCleanup founded_cleanup;
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
    // The tail's last press may have been HALF consumed: picker_main returned
    // between its DOWN and its UP. Poll that release through the engine --
    // reset_mouse_click_tracking() consumes the queued events before it
    // re-baselines -- instead of flushing it. A FLUSHED release leaves
    // mouse_state.left stuck true, so the next flow's first press makes no
    // up->down edge and simply evaporates (measured: the legacy-loop CONTINUE
    // burned its whole 10 s edge wait and landed only on the ladder's second
    // attempt). What the flush is still for is the rest: motion and any press
    // pushed after the menu stopped reading.
    reset_mouse_click_tracking();
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);

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
    reset_mouse_click_tracking();
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);
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

    ScopedCompanyFileCleanup founded_cleanup;
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
    reset_mouse_click_tracking();
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);

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
