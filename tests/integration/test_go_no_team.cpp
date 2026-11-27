#include <atomic>
#include <memory>
#include <array>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_click_ladder.h"
#include "test_escape_tail.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/gameplay/guy.h>
#include <openglad/resources/save_data.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {
// Leg ceilings, not settles: each bounds how long one screen may take to
// answer. Every wait in this file is on a condition (a published id, a
// completed menu frame, a trace), every click is proven consumed by the edge
// it opens (tests/test_click_ladder.h), and the last BACK is the escape
// tail's (tests/test_escape_tail.h), so a press that evaporates on a starved
// frame is sent again instead of leaving the main thread in the menu until
// the group's ceiling (#397).
constexpr int kMainMenuWaitMs = 5000;
// CONTINUE loads the company and its campaign level on the menu thread.
constexpr int kTeamDoorWaitMs = 10000;
// GO with no team answers with its popup on the frame that reads the click.
constexpr int kPopupWaitMs = 5000;
constexpr Uint32 kTracePollMs = 50;

template <typename Predicate>
bool wait_until(Predicate predicate, int timeout_ms, int poll_ms = kTracePollMs)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (predicate())
            return true;
        SDL_Delay(static_cast<Uint32>(poll_ms));
        elapsed += poll_ms;
    }
    return predicate();
}
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

// Test: Clicking GO with no team should show a popup error, not crash or start.
//
// Flow: Main Menu -> Continue -> GO -> (popup "NEED A TEAM!") -> OK -> Back -> Back
//
// Verifies:
//   1. GO button gracefully handles empty team
//   2. Shows popup dialog
//   3. Can continue navigating after the error

struct GoNoTeamState {
    bool started = false;
    bool finished = false;
    bool saw_popup = false;
    // Set by the MAIN thread after picker_main returns, never by the injector.
    std::atomic<bool> test_finished{false};
};

static int go_no_team_injector(void* data)
{
    og::runtime::ensure_thread_session();
    GoNoTeamState* state = static_cast<GoNoTeamState*>(data);
    state->started = true;

    // A leg that gives up hands the flow to the escape tail, which keeps
    // closing whatever screen is up until picker_main has returned.
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why);
    };

    // -- Leg 1: the main menu is up --
    if (!wait_for_interactable("continue_game", kMainMenuWaitMs) ||
        !wait_for_menu_frames(2))
        return escape(1, "the main menu never published continue_game");

    // -- Leg 2: CONTINUE opens base camp --
    fprintf(stderr, "  [test] clicking continue_game\n");
    if (!click_until_edge("continue_game",
                          [](int wait_ms) { return wait_for_interactable("go", wait_ms); },
                          /*landed_trace=*/nullptr, /*attempts=*/3,
                          /*wait_ms=*/kTeamDoorWaitMs) ||
        !wait_for_menu_frames(2))
        return escape(2, "CONTINUE never opened base camp");

    // -- Leg 3: GO with no team (THE RULE UNDER TEST) --
    // popup_dialog returns at once under TESTING and leaves its trace: the
    // trace is the edge. A press that left no trace did not land, so pressing
    // it again cannot start anything twice.
    fprintf(stderr, "  [test] clicking go (with empty team)\n");
    state->saw_popup = click_until_edge(
        "go",
        [](int wait_ms) {
            return wait_until([] { return trace_contains("popup", "NEED A TEAM"); }, wait_ms);
        },
        /*landed_trace=*/nullptr, /*attempts=*/3, /*wait_ms=*/kPopupWaitMs);
    if (!state->saw_popup)
        return escape(3, "GO with no team never showed NEED A TEAM");

    // -- Leg 4: base camp is still there to navigate (the popup is gone) --
    if (!wait_for_interactable("back", kTeamDoorWaitMs) || !wait_for_menu_frames(2))
        return escape(4, "base camp did not come back after the popup");

    state->finished = true;
    // BACK is the tail's: picker_main returns under it.
    fprintf(stderr, "  [test] leaving base camp through the escape tail\n");
    return escape(0, "");
}

TEST(GoNoTeam, go_without_team) {
    trace_clear();

    // Set up save with NO team members
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    GoNoTeamState state;
    SDL_Thread* thread = SDL_CreateThread(go_no_team_injector, "go_no_team", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    state.test_finished.store(true);
    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(state.started) << "the injector thread never ran";
    EXPECT_EQ(0, thread_result) << "the leg that gave up, by number (0: none)";
    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_popup) << "should have seen 'NEED A TEAM!' popup";
}



// §2.5: the base camp has no TRAIN TEAM strip button — training enters
// through the per-row TRAIN buttons, which do not exist while the roster is
// empty (the layout BFS matrix pins the hidden-row shape). The need-a-team
// guard still protects every entry into the train screen: calling it with
// an empty roster pops the NEED A TEAM dialog and returns without opening.

TEST(GoNoTeam, train_without_team) {
    trace_clear();

    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.team_size = 0;
    for (auto& slot : og::runtime::current_session->myscreen_->save_data.team_list)
        slot.reset();

    const Sint32 ret = create_train_menu(0);
    ASSERT_EQ(4, (int)ret) << "empty-roster train entry should return OK";
    ASSERT_TRUE(trace_contains("popup", "NEED A TEAM"))
        << "should have seen 'NEED A TEAM!' popup";
}


// §4.3 rule 4: GO counts DEPLOYED heroes per seat team. Two seats sharing
// one hero leaves a player with nothing to control, so GO refuses by name
// and no game starts — the previous behaviour dropped a player into a level
// with no character.
extern std::atomic<int> g_test_game_epoch;

TEST(GoNoTeam, go_with_an_unmanned_seat_is_refused)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 2;
    save.my_team = 1;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    for (auto& slot : save.team_list)
        slot.reset();

    // Paired control: the same two seats with a hero nobody deployed gets
    // the DEPLOY refusal, so the popup text really is derived from the
    // roster and not a constant.
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->teamnum = 1;
    save.team_list[0]->deployed = false;
    save.team_size = 1;

    trace_clear();
    const int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    EXPECT_EQ(2, static_cast<int>(go_menu(0)))
        << "a benched roster returns MENU_REDRAW";
    EXPECT_TRUE(trace_contains("popup",
                               "DEPLOY AT LEAST ONE: Deploy at least"))
        << "benched roster must get the deploy popup";

    // One deployed hero, two seats: the second seat has nothing to control.
    save.team_list[0]->deployed = true;
    trace_clear();
    EXPECT_EQ(2, static_cast<int>(go_menu(0)))
        << "an unmanned seat returns MENU_REDRAW instead of launching";
    EXPECT_TRUE(trace_contains("popup",
                               "DEPLOY FOR EVERY PLAYER: Each player needs"))
        << "the second seat's refusal names itself";
    EXPECT_FALSE(trace_contains("popup", "DEPLOY AT LEAST ONE"))
        << "the deployed hero cleared the earlier guard";
    EXPECT_EQ(epoch_before, g_test_game_epoch.load(std::memory_order_acquire))
        << "no level may start while a seat is unmanned";

    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    save.numplayers = 1;
    save.my_team = 0;
}
