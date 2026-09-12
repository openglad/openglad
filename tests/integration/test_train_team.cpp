#include <memory>
#include <array>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/button.h>
#include <openglad/core/test_trace.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_common.h>
#include <openglad/core/util.h>
#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


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

// Test: Continue -> base camp roster TRAIN (row 0, §2.5) -> stat buttons -> Back -> Back
//
// Verifies:
//   1. Train menu opens when team has members
//   2. Can click stat increase buttons
//   3. Can cycle through team members
//   4. Navigation back works

struct TrainState {
    bool started;
    bool finished;
    bool saw_train_menu;
};

static int train_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TrainState* state = static_cast<TrainState*>(data);
    state->started = true;

    // Wait for main menu
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    // Wait for team menu
    SDL_Delay(500);
    wait_for_interactable("roster_row_0", 10000);
    SDL_Delay(750);

    // Click the roster row body (§9.11: the row IS the train affordance)
    fprintf(stderr, "  [test] clicking roster_row_0 (§9.11 row-click train)\n");
    interact("roster_row_0");

    // Wait for train menu buttons
    SDL_Delay(500);
    if (wait_for_interactable("inc_str", 10000)) {
        state->saw_train_menu = true;
        SDL_Delay(500);

        // Try increasing strength
        fprintf(stderr, "  [test] clicking inc_str\n");
        interact("inc_str");
        SDL_Delay(300);

        // Try increasing dexterity
        fprintf(stderr, "  [test] clicking inc_dex\n");
        interact("inc_dex");
        SDL_Delay(300);

        // Open details across several classes to exercise detail rendering branches.
        for (int i = 0; i < 5; i++) {
            wait_for_interactable("details", 10000);
            fprintf(stderr, "  [test] clicking details (%d)\n", i + 1);
            interact("details");
            SDL_Delay(300);
            wait_for_interactable("back", 10000);
            fprintf(stderr, "  [test] clicking back from details (%d)\n", i + 1);
            interact("back");
            SDL_Delay(300);

            if (i < 4) {
                fprintf(stderr, "  [test] clicking next (%d)\n", i + 1);
                interact("next");
                SDL_Delay(300);
            }
        }

        // Go back
        fprintf(stderr, "  [test] clicking back from train menu\n");
        interact("back");
    }

    // Back in team menu
    SDL_Delay(500);
    wait_for_interactable("back", 10000);
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    // Occasionally the final BACK click can be missed (menu-loop timing). Keep
    // nudging Escape/BACK until we see main menu again so the test can't hang.
    const Uint64 deadline = SDL_GetTicks() + 8000;
    while (SDL_GetTicks() < deadline)
    {
        // If the main menu is back, we are done.
        if (wait_for_interactable("continue_game", 150))
            break;

        // Prefer Escape since BACK has KEYSTATE_ESCAPE in most picker menus.
        inject_key_press(SDLK_ESCAPE, 10);
        SDL_Delay(50);

        // If we're still in a submenu with a BACK button, click it again.
        if (wait_for_interactable("back", 150))
        {
            fprintf(stderr, "  [test] retry clicking back\n");
            interact("back");
            SDL_Delay(150);
        }
    }

    state->finished = true;
    return 0;
}

TEST(TrainTeam, train_team) {
    trace_clear();

    // Set up a team with members so train menu doesn't show "NEED A TEAM!" popup
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    og::runtime::current_session->myscreen_->save_data.totalcash = 50000;  // Enough cash for training

    // Add multiple classes at high level to hit create_detail_menu text branches.
    auto archmage = std::make_unique<guy>(FAMILY_ARCHMAGE);
    auto cleric = std::make_unique<guy>(FAMILY_CLERIC);
    auto druid = std::make_unique<guy>(FAMILY_DRUID);
    auto thief = std::make_unique<guy>(FAMILY_THIEF);
    auto orc = std::make_unique<guy>(FAMILY_ORC);
    archmage->level = 10;
    cleric->level = 10;
    druid->level = 10;
    thief->level = 10;
    orc->level = 10;
    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(archmage);
    og::runtime::current_session->myscreen_->save_data.team_list[1] = std::move(cleric);
    og::runtime::current_session->myscreen_->save_data.team_list[2] = std::move(druid);
    og::runtime::current_session->myscreen_->save_data.team_list[3] = std::move(thief);
    og::runtime::current_session->myscreen_->save_data.team_list[4] = std::move(orc);
    og::runtime::current_session->myscreen_->save_data.team_size = 5;

    og::runtime::current_session->myscreen_->save_data.save("save0");

    TrainState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(train_injector, "train_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_train_menu) << "should have entered the train menu";
}

// ---------------------------------------------------------------------------
// The TRAIN screen's content pass runs on a LIVE session: run_menu_screen
// polls the lobby at the top of every frame for a polls_lobby spec, and
// LocalPickerLobbyClient::apply_state_to_save resets every save.team_list
// slot before rebuilding the roster from its cached lobby state. So a frame
// can arrive with the trained member already gone. Every TrainSession
// accessor the pass reads is null-guarded except original(), which is a bare
// `return *original_member();` — the draw pass must not reach it.
// ---------------------------------------------------------------------------

// TrainEngineState is file-local to picker_team_build.cpp; the whole of it is
// the start_time the content pass reads (picker_team_build.cpp, "Per-open
// screen state (the legacy loop's locals)"), so the screen_state a test hands
// the pass is layout-compatible with it.
struct TrainEngineStateMirror {
    Sint32 start_time = 0;
};

void picker_train_menu_engine_draw_content(void* screen_state);

TEST(TrainMenuDraw, draw_content_survives_the_member_vanishing_under_a_lobby_poll)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_MAGE);
    save.team_list[0]->name = "VANISHER";
    save.team_list[0]->level = 6;
    save.team_size = 1;

    og::ui::TrainSession session(save);
    og::runtime::current_session->current_guy_ =
        std::make_unique<guy>(*save.team_list[0]);
    pks().train_session = &session;
    ASSERT_FALSE(session.empty()) << "the session starts on a real member";

    // What one lobby poll does to the roster, verbatim
    // (picker_lobby_client.cpp, apply_state_to_save).
    for (auto& member : save.team_list)
        member.reset();
    save.team_size = 0;
    ASSERT_TRUE(session.empty()) << "the session must see its member is gone";

    // The guarded siblings already agree the member is gone...
    EXPECT_EQ(0u, session.current_cost());
    EXPECT_FALSE(session.level_increased());

    // ... and the draw pass must agree too instead of dereferencing the null
    // original (SEGV at picker_team_build.cpp's stat table on the unfixed
    // tree).
    TrainEngineStateMirror state;
    state.start_time = query_timer();
    picker_train_menu_engine_draw_content(&state);

    pks().train_session = nullptr;
    og::runtime::current_session->current_guy_.reset();
    save.reset();
    SUCCEED() << "the draw pass returned instead of dereferencing a null "
                 "original";
}
