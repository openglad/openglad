#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/test_trace.h>
#include "test_framework.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/core/util.h>

#include <atomic>

// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#ifdef TESTING
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
#endif

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

// Picker globals that can leak across integration tests and affect menu start state

// FAERIE is at index 12 in allowable_guys[]
#define FAERIE_INDEX 12

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

    // Ensure the next test starts the hire menu from a clean state.
    og::runtime::current_session->current_guy_.reset();
    pks().old_guy = nullptr;      // Non-owning; may point into team_list[]
    og::runtime::current_session->current_type_ = 0;
    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->current_team_num_ = 0;
}

// Test: hire a lone fairy via the UI, start level 4 at max speed, stand there,
// and confirm we lose.
//
// Flow:
//   Main Menu -> Begin New Game -> (dismiss campaign intro) ->
//   Hire Menu -> NEXT x12 to reach FAERIE -> HIRE ME -> BACK ->
//   Team Menu -> (set scen_num=4) -> GO -> game runs -> fairy dies -> BACK -> exits
//
// Uses level 4 because levels 1-3 have team0 NPC allies that keep the
// game alive after the fairy dies (view.cpp hands control to any team0
// entity). Level 4 has no team0 NPCs, so endgame(1) fires immediately
// when the fairy dies.
//
// The fairy has the lowest HP of all characters. With no input, enemies
// swarm and kill it quickly.

struct FairyState {
    bool started;
    bool finished;
    float original_speed;
};

static int fairy_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FairyState* state = static_cast<FairyState*>(data);
    state->started = true;

    // -- Main Menu --
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(1500);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // Dismiss campaign intro screen (blocks until Escape)
    SDL_Delay(1000);
    fprintf(stderr, "  [test] dismissing campaign intro\n");
    inject_key_press(SDLK_ESCAPE);

    // popup_dialog("HIRE TROOPS") returns immediately under TESTING
    SDL_Delay(500);
    wait_for_interactable("hire_me", 10000);
    SDL_Delay(1500);

    // -- Hire Menu: cycle to FAERIE and hire --
    // Starts at allowable_guys[0] (SOLDIER). Click NEXT 12 times for FAERIE.
    fprintf(stderr, "  [test] cycling to fairy (NEXT x%d)\n", FAERIE_INDEX);
    for (int i = 0; i < FAERIE_INDEX; i++) {
        interact("next");
        SDL_Delay(200);
    }

    fprintf(stderr, "  [test] hiring fairy\n");
    interact("hire_me");
    SDL_Delay(300);

    fprintf(stderr, "  [test] clicking back from hire menu\n");
    interact("back");

    // -- Team Menu: GO at max speed --
    SDL_Delay(500);
    wait_for_interactable("go", 10000);
    SDL_Delay(500);

    og::runtime::current_session->myscreen_->save_data.scen_num = 4;
    set_game_speed(0.0f);

    fprintf(stderr, "  [test] clicking go\n");
    int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    interact("go");

    // -- Wait for glad_main to finish --
    // Old buttons from create_team_menu persist through glad_main, so
    // wait_for_interactable("back") would return immediately (stale buttons).
    // Instead, wait for a monotonic epoch increment and then poll g_test_in_game
    // which is set/cleared around glad_main.
    fprintf(stderr, "  [test] waiting for game to finish...\n");
    {
        int waited_ms = 0;
        const int poll_ms = 50;
        while (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before && waited_ms < 10000) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before) {
            fprintf(stderr, "  [test] ERROR: game never started (epoch unchanged)\n");
            set_game_speed(state->original_speed);
            return 0;
        }
        waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire) && waited_ms < 60000) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_in_game.load(std::memory_order_acquire)) {
            fprintf(stderr, "  [test] ERROR: game did not finish within timeout\n");
            set_game_speed(state->original_speed);
            return 0;
        }
    }

    // Restore test settings
    set_game_speed(state->original_speed);

    // Now we're truly back in create_team_menu with fresh buttons
    wait_for_interactable("back", 10000);
    SDL_Delay(1500);

    // Exit team menu -> main menu -> picker exits
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

TEST(FairyDeath, fairy_death) {
    trace_clear();

    // Some integration tests leave the picker globals set, which changes the
    // starting class in the hire menu. Reset here so NEXT x12 always lands on FAERIE.
    og::runtime::current_session->current_guy_.reset();
    pks().old_guy = nullptr;
    og::runtime::current_session->current_type_ = 0;
    og::runtime::current_session->editguy_ = 0;
    og::runtime::current_session->current_team_num_ = 0;

    // Start with empty team
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "org.openglad.gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    FairyState state = { false, false, og::runtime::current_session->g_game_speed_factor_ };
    SDL_Thread* thread = SDL_CreateThread(fairy_injector, "fairy_injector", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    // picker_main blocks — the injector thread drives all navigation
    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";

    // We lost — level 4 should NOT be marked completed
    ASSERT_TRUE(!og::runtime::current_session->myscreen_->save_data.is_level_completed(4)) << "level 4 should NOT be completed (fairy should have died)";

    fprintf(stderr, "  [test] Fairy died as expected via UI hire flow\n");
}

