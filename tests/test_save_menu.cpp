#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/core/test_trace.h>
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[i].reset();
        pks().backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    pks().main_columns_pix.reset();
    pks().main_columns_data.free();
    pks().main_title_logo_pix.reset();
    pks().main_title_logo_data.free();
}

// Test: Navigate to Save Team menu, see the save slots, then exit.
//
// Note: We can't test actually saving via menu because do_save() calls
// prompt_for_string() which requires keyboard text input. But we CAN
// verify the save menu opens and has the right buttons.
//
// Flow: Main Menu -> Continue -> Save Team -> Back -> Back

struct SaveMenuState {
    bool started;
    bool finished;
    bool saw_save_menu;
};

static int save_menu_injector(void* data)
{
    og::runtime::ensure_thread_session();
    SaveMenuState* state = static_cast<SaveMenuState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("save_team", 10000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking save_team\n");
    interact("save_team");

    // Save menu has save_slot_1 through save_slot_10 and back
    SDL_Delay(500);
    if (wait_for_interactable("save_slot_1", 10000)) {
        state->saw_save_menu = true;
        SDL_Delay(500);

        // Just verify the slots exist, don't actually try to save
        // (that would trigger prompt_for_string)
        fprintf(stderr, "  [test] clicking back from save menu\n");
        interact("back");
    }

    // Back in team menu
    SDL_Delay(2000);
    wait_for_interactable("back", 10000);
    SDL_Delay(500);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

TEST(SaveMenu, save_team_menu) {
    trace_clear();

    // Need some team data
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;

    og::runtime::current_session->myscreen_->save_data.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    SaveMenuState state = { false, false, false };
    SDL_Thread* thread = SDL_CreateThread(save_menu_injector, "save_menu_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_save_menu) << "should have seen the save team menu";
}

