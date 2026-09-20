#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_camp_save_fixture.h"
#include "test_click_ladder.h"
#include "test_company_cleanup.h"
#include "test_escape_tail.h"
#include "test_frame_capture.h"
#include "test_interact.h"
#include <openglad/resources/io_common.h>
#include <openglad/resources/packs.h>
#include <openglad/resources/company.h>
#include <openglad/resources/save_data.h>
#include <atomic>
#include <cstdlib>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


// Regression test for segfault: Main Menu -> Continue -> Level Progress
//
// Reproduces the exact crash path by clicking actual UI buttons:
//   1. picker_main() enters the main menu
//   2. We click "CONTINUE GAME" button -> enters create_team_menu
//   3. We click "PROGRESS" button -> enters create_progress_menu
//      (this is where the segfault used to occur in getLevelStats)
//   4. We click "BACK" -> unwinds all menus back to picker_main
//
// A background thread uses the interaction API to drive navigation
// while the main thread runs the blocking menu event loops.

struct EventSequence {
    bool started;
    bool finished;
    // Every edge the flow must actually reach. The injector records the
    // return of its own waits/clicks so the test body can ASSERT on them:
    // without this a screen that never opens only makes the injector time
    // out through its waits, and the test stays green (slowly).
    bool continue_clicked;
    bool scenario_open;
    bool scenario_clicked;
    bool progress_clicked;
    bool progress_open;
    bool progress_back_clicked;
    bool scenario_reopened;
};

static int event_injector_thread(void* data)
{
    og::runtime::ensure_thread_session();
    EventSequence* seq = static_cast<EventSequence*>(data);
    seq->started = true;

    // Wait for mainmenu to initialize. Its depth-1 entry fades (#237
    // derivation), but fades are INSTANT under TESTING — the 750ms is a
    // generic settle for the runner loop, not fade timing.
    wait_for_interactable("continue_game", 5000);
    wait_for_menu_frames(2);

    // Step 1: Click "CONTINUE GAME"
    fprintf(stderr, "  [test] Step 1: clicking continue_game\n");
    seq->continue_clicked = interact("continue_game");

    // Wait for create_team_menu to init after fade + level load. Base Camp's
    // depth-1 entry fades (a fade-out unless a teardown noted the surface
    // black, the cold compose, a fade-in); the level reload happens in the
    // runner loop's reload guard.
    // PROGRESS lives inside the SCENARIO subscreen now.
    seq->scenario_open = wait_for_interactable("scenario", 10000);
    wait_for_menu_frames(2);

    // Step 2: Open the SCENARIO subscreen, then click "PROGRESS"
    fprintf(stderr, "  [test] Step 2: clicking scenario, then progress\n");
    seq->scenario_clicked = interact("scenario");
    wait_for_interactable("progress", 10000);
    wait_for_menu_frames(2);
    seq->progress_clicked = interact("progress");

    // Wait for create_progress_menu to load level data and enter its loop.
    // "prev" is unique to the progress menu here, so waiting on it (instead
    // of the ambiguous per-screen "back") confirms the right screen is up.
    seq->progress_open = wait_for_interactable("prev", 10000);
    wait_for_menu_frames(2);

    // Step 3: Click "BACK" in the progress menu
    fprintf(stderr, "  [test] Step 3: clicking progress back\n");
    seq->progress_back_clicked = interact("back");

    // create_progress_menu returns REDRAW, so we're back in the SCENARIO
    // subscreen. Wait for its unique "view_scenario" button, then BACK out.
    seq->scenario_reopened = wait_for_interactable("view_scenario", 10000);
    wait_for_menu_frames(2);

    // Step 4: Click "BACK" in the scenario menu
    fprintf(stderr, "  [test] Step 4: clicking scenario back\n");
    interact("back");

    // Step 5: Click "BACK" in the team menu
    wait_for_interactable("scenario", 10000);
    wait_for_menu_frames(2);
    fprintf(stderr, "  [test] Step 5: clicking team back\n");
    interact("back");

    // Back returns to the main menu. Trip the test-mode max-loop guard so
    // picker_main() does not sit in a fresh mainmenu() waiting for input.
    g_picker_mainmenu_calls = g_picker_max_mainmenu_calls;

    seq->finished = true;
    return 0;
}

static void cleanup_picker_state()
{
    // Clean up picker globals without deleting myscreen
    // (picker_quit() deletes myscreen which other tests still need)
    for (int i = 0; i < 5; i++) {
        pks().backdrops[static_cast<std::size_t>(i)].reset();
        pks().backpics[i].free();
    }
    // localbuttons == allbuttons[0] (returned by init_buttons), so just
    // delete everything via allbuttons to avoid double-free.
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

TEST(LevelProgress, menu) {
    trace_clear();

    // CONTINUE opens the MOST RECENT company on disk, not the one this test
    // wrote: a bare SaveData::save() never stamps last_played_unix_s, so a
    // company another test founded would take the session over silently.
    // Seed through the autosave choke point that stamps, and check after the
    // flow which company it actually got.
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";

    // Set up save data so "CONTINUE GAME" has something to load
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(seed_open_company(save, "save0", newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";

    // Start the event injector thread
    EventSequence seq = {};
    SDL_Thread* thread = SDL_CreateThread(event_injector_thread, "test_injector", &seq);
    ASSERT_TRUE(thread != nullptr) << "failed to create event injector thread";

    // Limit picker_mainmenu_loop to 1 iteration (this test only needs one pass)
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    // This blocks until menus unwind -- the injector thread drives navigation
    picker_main(0, nullptr);

    // Wait for thread to finish
    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;  // reset for other tests

    ASSERT_EQ("save0", og::data::active_company_slot())
        << "the flow must have run on the company this test seeded";

    // If we got here without crashing, the regression test passed
    ASSERT_TRUE(seq.finished) << "event injector thread should have completed";

    // Every edge of CONTINUE -> SCENARIO -> PROGRESS -> BACK actually
    // happened. trace_contains("menu", "init_buttons") could not tell these
    // apart: button.cpp emits it for the main menu too, so it stays green
    // even when SCENARIO or PROGRESS never opens and the injector merely
    // times out through its waits. "prev" is the progress menu's own row on
    // this path (kProgressMenuRows), so waiting on it names the screen.
    ASSERT_TRUE(seq.continue_clicked) << "CONTINUE GAME must have been clicked";
    ASSERT_TRUE(seq.scenario_open)
        << "Base Camp must have exposed its SCENARIO door";
    ASSERT_TRUE(seq.scenario_clicked) << "SCENARIO must have been clicked";
    ASSERT_TRUE(seq.progress_clicked) << "PROGRESS must have been clicked";
    ASSERT_TRUE(seq.progress_open)
        << "the progress menu must have opened (its own 'prev' row)";
    ASSERT_TRUE(seq.progress_back_clicked)
        << "BACK must have been clicked inside the progress menu";
    ASSERT_TRUE(seq.scenario_reopened)
        << "BACK from progress must land back on the SCENARIO subscreen";
}

// ---------------------------------------------------------------------------
// R2-4: no progress vocabulary on a Multiplayer Arenas campaign.
//
// The maintainer's question — "why do we care, for multiplayer levels?" —
// is answered at ONE predicate (og::ui::progress_marks_shown), and the
// PROGRESS screen one door off the versus Base Camp is the loudest of its
// readers: a report headed "Level Progress: 1 cleared of 40 discovered"
// with a green CLEARED column is the same complaint the wizard's readout
// was. On a versus campaign the header states the roll ("Arenas: 40") and
// every row wears CURRENT or the blank dashes, never CLEARED, so every row
// wears GO rather than the REPLAY/VISIT pair an earned road gets.
//
// The oracle is the screen's own derivation and composition traces, read on
// a save that HAS a completed arena (821): without that the pin would pass
// on a campaign with nothing to mark.

namespace {

struct VersusProgressState
{
    std::atomic<bool> test_finished{false};
    bool camp_seen = false;
    bool scenario_open = false;
    bool progress_open = false;
    bool finished = false;
    int captures = 0;
};

int versus_progress_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<VersusProgressState*>(data);
    // In precedence order: the PROGRESS report's own BACK (named by its
    // unique `prev` row), the SCENARIO submenu's (named by VIEW LEVEL),
    // Base Camp's, then the picker's way forward.
    const auto escape = [state](int leg, const char* why) {
        static constexpr EscapeDoor kProgressDoors[] = {
            {"prev", "back"},
            {"view_scenario", "back"},
            {"go", "back"},
            {"continue_game", "continue_game"},
        };
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kProgressDoors);
    };

    state->camp_seen = wait_for_interactable("continue_game", 10000);
    if (!state->camp_seen)
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    if (!click_until_edge("continue_game", [](int wait_ms) {
            return wait_for_interactable("scenario", wait_ms);
        }))
    {
        return escape(2, "Base Camp never came up");
    }
    state->scenario_open = true;
    if (!click_until_edge("scenario", [](int wait_ms) {
            return wait_for_interactable("progress", wait_ms);
        }))
    {
        return escape(3, "the SCENARIO submenu never came up");
    }
    // `prev` is the report's own row, so waiting on it names the screen.
    state->progress_open = click_until_edge("progress", [](int wait_ms) {
        return wait_for_interactable("prev", wait_ms);
    });
    if (!state->progress_open)
        return escape(4, "the PROGRESS report never came up");
    (void)wait_for_menu_frames(2);
    capture_presented_frame("progress_modes", std::getenv("UXSHOTS_DIR"));
    ++state->captures;

    state->finished = true;
    return escape(0, "");
}

} // namespace

TEST(LevelProgress, versus_progress_report_carries_no_progress_word)
{
    trace_clear();
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    // Cursor on THE PITCH (820) with THE MUDBOWL (821) already played:
    // the arena the classic rule would paint green.
    write_save0_with_two_soldiers("modes", 820, {821});
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    ASSERT_TRUE(seed_open_company(save, "save0", newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";

    VersusProgressState state;
    SDL_Thread* thread =
        SDL_CreateThread(versus_progress_injector, "versus_progress", &state);
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
    ASSERT_TRUE(state.progress_open)
        << "the PROGRESS report must have opened on the modes company";
    // The report lists the ACCESSIBLE set, which on this company is the
    // campaign's entry arena (300), the cursor (820) and the one played
    // (821) — a pre-existing rule this round does not touch. What R2-4
    // changes is the vocabulary: the header states the roll it is showing
    // instead of scoring it.
    EXPECT_TRUE(trace_contains("progress", "header Arenas: 3"))
        << "the versus header states the roll, not a score";
    EXPECT_FALSE(trace_contains("progress", "cleared of"))
        << "'n cleared of m' is exactly the vocabulary R2-4 removes";
    EXPECT_TRUE(trace_contains("progress", "row 820 CURRENT"))
        << "[CURRENT] is not progress vocabulary and stays";
    EXPECT_TRUE(trace_contains("progress", "row 821 -------"))
        << "a completed arena wears the blank status on a versus campaign, "
           "so its row wears GO like every other";
    EXPECT_FALSE(trace_contains("progress", "CLEARED"))
        << "no CLEARED cell anywhere in the report";
    verify_captured_frames("versus_progress", 1);

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    save.current_campaign = "gladiator";
    save.scen_num = 1;
}

// ...and the classic campaign is untouched: the same report on gladiator
// still counts what the company earned. Without this leg the change above
// could have deleted the vocabulary outright.
TEST(LevelProgress, classic_progress_report_still_counts_what_was_cleared)
{
    trace_clear();
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    write_save0_with_two_soldiers("gladiator", 1, {1});
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    ASSERT_TRUE(seed_open_company(save, "save0", newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";

    VersusProgressState state;
    SDL_Thread* thread =
        SDL_CreateThread(versus_progress_injector, "classic_progress", &state);
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
    ASSERT_TRUE(state.progress_open);
    EXPECT_TRUE(trace_contains("progress", "header Level Progress:"))
        << "a classic campaign still reports what the company cleared";
    EXPECT_TRUE(trace_contains("progress", "cleared of"));
    EXPECT_TRUE(trace_contains("progress", "row 1 CLEARED"))
        << "and the cleared level still wears the word";
    verify_captured_frames("classic_progress", 1);
    save.scen_num = 1;
}
