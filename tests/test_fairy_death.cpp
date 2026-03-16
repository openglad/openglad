#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL.h>
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
void picker_testing_yes_or_no_queue_clear();
void picker_testing_yes_or_no_queue_push(bool value);

#ifdef TESTING
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
#endif

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {
constexpr Uint32 kUiSettleMs = 150;
constexpr Uint32 kMenuTransitionMs = 250;
constexpr Uint32 kCycleStepMs = 100;
constexpr int kGameStartTimeoutMs = 20000;
constexpr int kFairyDeathTimeoutMs = 60000;
// The transport-backed gameplay loop is materially slower under validation and
// sanitizers than the legacy direct tick path. Keep the UI test budget aligned
// with the runtime we now exercise in CI instead of failing early.
constexpr int kGameAbortTimeoutMs = 20000;
constexpr short kFairyFragileConstitution = -20;
constexpr short kFairyFragileArmor = -100;
}

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
// Uses level 4 because the fairy starts near hostiles and dies quickly with
// no player input once its stats are weakened. After confirming that in-world
// death, the test exits through the normal "Abort Mission" prompt to unwind
// back to the picker without mutating runtime state out of band.
//
// Before starting the level the test makes the hired fairy deliberately
// fragile, but still lets normal gameplay deliver the defeat.

struct FairyState {
    bool started;
    bool finished;
    float original_speed;
};

enum class FairyLifeState {
    NotSpawned,
    Alive,
    DeadOrGone,
};

static FairyLifeState query_hired_fairy_life_state()
{
    screen* const screen = og::runtime::current_session->myscreen_;
    if (screen == nullptr)
        return FairyLifeState::NotSpawned;

    for (const auto& entity_up : screen->world().oblist) {
        walker* const entity = entity_up.get();
        if (entity == nullptr || entity->myguy == nullptr)
            continue;
        if (entity->myguy->family != FAMILY_FAERIE || entity->myguy->teamnum != 0)
            continue;
        if (entity->dead() ||
            (entity->stats() != nullptr && entity->stats()->hitpoints() <= 0.0f)) {
            return FairyLifeState::DeadOrGone;
        }
        return FairyLifeState::Alive;
    }

    return FairyLifeState::NotSpawned;
}

static int fairy_injector(void* data)
{
    og::runtime::ensure_thread_session();
    FairyState* state = static_cast<FairyState*>(data);
    state->started = true;

    // -- Main Menu --
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(kUiSettleMs);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // Dismiss campaign intro screen (blocks until Escape)
    SDL_Delay(kMenuTransitionMs);
    fprintf(stderr, "  [test] dismissing campaign intro\n");
    inject_key_press(SDLK_ESCAPE);

    // popup_dialog("HIRE TROOPS") returns immediately under TESTING
    SDL_Delay(kUiSettleMs);
    wait_for_interactable("hire_me", 10000);
    SDL_Delay(kUiSettleMs);

    // -- Hire Menu: cycle to FAERIE and hire --
    // Starts at allowable_guys[0] (SOLDIER). Click NEXT 12 times for FAERIE.
    fprintf(stderr, "  [test] cycling to fairy (NEXT x%d)\n", FAERIE_INDEX);
    for (int i = 0; i < FAERIE_INDEX; i++) {
        interact("next");
        SDL_Delay(kCycleStepMs);
    }

    fprintf(stderr, "  [test] hiring fairy\n");
    interact("hire_me");
    SDL_Delay(kCycleStepMs);

    fprintf(stderr, "  [test] clicking back from hire menu\n");
    interact("back");

    // -- Team Menu: GO at max speed --
    SDL_Delay(kUiSettleMs);
    wait_for_interactable("go", 10000);
    SDL_Delay(kUiSettleMs);

    if (og::runtime::current_session->myscreen_->save_data.team_size < 1 ||
        !og::runtime::current_session->myscreen_->save_data.team_list[0]) {
        fprintf(stderr, "  [test] ERROR: expected hired fairy before starting level\n");
        set_game_speed(state->original_speed);
        return 0;
    }
    guy* fairy = og::runtime::current_session->myscreen_->save_data.team_list[0].get();
    fairy->constitution = kFairyFragileConstitution;
    fairy->armor = kFairyFragileArmor;

    og::runtime::current_session->myscreen_->save_data.scen_num = 4;
    set_game_speed(0.0f);

    fprintf(stderr, "  [test] clicking go\n");
    int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    interact("go");

    // -- Wait for the hired fairy to die, then unwind through the normal
    // abort-mission prompt --
    fprintf(stderr, "  [test] waiting for fairy to die...\n");
    {
        int waited_ms = 0;
        const int poll_ms = 50;
        while (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before
               && waited_ms < kGameStartTimeoutMs) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before) {
            fprintf(stderr, "  [test] ERROR: game never started (epoch unchanged)\n");
            set_game_speed(state->original_speed);
            return 0;
        }

        bool saw_fairy_alive = false;
        waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire)
               && waited_ms < kFairyDeathTimeoutMs) {
            const FairyLifeState fairy_state = query_hired_fairy_life_state();
            if (fairy_state == FairyLifeState::Alive)
                saw_fairy_alive = true;
            if (saw_fairy_alive && fairy_state != FairyLifeState::Alive)
                break;
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (!saw_fairy_alive ||
            query_hired_fairy_life_state() == FairyLifeState::Alive) {
            fprintf(stderr, "  [test] ERROR: fairy never died within timeout\n");
            set_game_speed(state->original_speed);
            return 0;
        }

        fprintf(stderr, "  [test] fairy died, aborting mission through UI\n");
        picker_testing_yes_or_no_queue_clear();
        picker_testing_yes_or_no_queue_push(true);
        inject_key_press(SDLK_ESCAPE);

        waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire)
               && waited_ms < kGameAbortTimeoutMs) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_in_game.load(std::memory_order_acquire)) {
            fprintf(stderr, "  [test] ERROR: game did not exit after abort prompt\n");
            set_game_speed(state->original_speed);
            return 0;
        }
    }

    // Restore test settings
    set_game_speed(state->original_speed);

    // Now we're truly back in create_team_menu with fresh buttons
    wait_for_interactable("back", 10000);
    SDL_Delay(kUiSettleMs);

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
