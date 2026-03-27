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
extern std::atomic<int> g_test_game_frame_ticks;
#endif

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {
constexpr Uint32 kUiSettleMs = 150;
constexpr Uint32 kMenuTransitionMs = 250;
constexpr Uint32 kCycleStepMs = 100;
constexpr int kGameStartTimeoutMs = 20000;
constexpr int kFairyDeathTimeoutMs = 60000;
constexpr int kGameFinishTimeoutMs = 20000;
constexpr int kGameAbortTimeoutMs = 20000;
constexpr short kFairyFragileConstitution = -20;
constexpr short kFairyFragileArmor = -100;
constexpr int kFairyPollMs = 10;
constexpr int kTeamMenuBackGameX = 60;
constexpr int kTeamMenuBackGameY = 155;
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

// Test: hire a lone fairy via the UI, start level 4 at max speed, confirm the
// fairy dies in-world, then unwind back to the picker.
//
// Flow:
//   Main Menu -> Begin New Game -> (dismiss campaign intro) ->
//   Hire Menu -> NEXT x12 to reach FAERIE -> HIRE ME -> BACK ->
//   Team Menu -> (set scen_num=4) -> GO -> game runs -> fairy dies ->
//   natural defeat resolves -> BACK -> exits
//
// Uses level 4 because the fairy starts near hostiles and dies quickly with no
// player input once its stats are weakened. When the defeat resolves before
// the injector thread can observe the exact death frame, the test still proves
// the natural death path by checking for the generic defeat popup.
//
// Before starting the level the test makes the hired fairy deliberately
// fragile, but still lets normal gameplay deliver the defeat.

struct FairyState {
    bool started;
    bool finished;
    bool observed_natural_death = false;
    bool saw_generic_defeat = false;
    float original_speed;
    const char* failure_message = nullptr;
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
        if (entity->myguy->family != FAMILY_FAERIE ||
            entity->myguy->teamnum != 0) {
            continue;
        }
        if (entity->dead() ||
            (entity->stats() != nullptr &&
             entity->stats()->hitpoints() <= 0.0f)) {
            return FairyLifeState::DeadOrGone;
        }
        return FairyLifeState::Alive;
    }

    return FairyLifeState::NotSpawned;
}

static void unwind_picker_after_failure(FairyState* state)
{
    set_game_speed(state->original_speed);
    picker_testing_yes_or_no_queue_clear();

    if (g_test_in_game.load(std::memory_order_acquire)) {
        picker_testing_yes_or_no_queue_push(true);
        inject_key_press(SDLK_ESCAPE);
        SDL_Delay(kUiSettleMs);
        inject_key_press(SDLK_ESCAPE);

        int waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kGameAbortTimeoutMs) {
            SDL_Delay(kFairyPollMs);
            waited_ms += kFairyPollMs;
        }
        picker_testing_yes_or_no_queue_clear();
    }

    for (int step = 0; step < 2; ++step) {
        if (wait_for_interactable("begin_new_game", 1000))
            return;
        if (!wait_for_interactable("back", 2000))
            return;
        SDL_Delay(kUiSettleMs);
        interact("back");
        SDL_Delay(kUiSettleMs);
    }
}

static int fail_fairy_run(FairyState* state, const char* message)
{
    fprintf(stderr, "  [test] ERROR: %s\n", message);
    state->failure_message = message;
    unwind_picker_after_failure(state);
    return 0;
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

    // New games now land on team build first, then enter hire explicitly.
    SDL_Delay(kUiSettleMs);
    wait_for_interactable("hire_troops", 10000);
    SDL_Delay(kUiSettleMs);
    interact("hire_troops");
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
        return fail_fairy_run(state,
                              "expected hired fairy before starting level");
    }

    guy* const fairy =
        og::runtime::current_session->myscreen_->save_data.team_list[0].get();
    if (fairy->family != FAMILY_FAERIE)
        return fail_fairy_run(state, "expected lone hired unit to be the fairy");

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
        while (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before
               && waited_ms < kGameStartTimeoutMs) {
            SDL_Delay(kFairyPollMs);
            waited_ms += kFairyPollMs;
        }
        if (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before) {
            return fail_fairy_run(state, "game never started (epoch unchanged)");
        }

        waited_ms = 0;
        while (g_test_game_frame_ticks.load(std::memory_order_acquire) == 0 &&
               g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kGameStartTimeoutMs) {
            SDL_Delay(kFairyPollMs);
            waited_ms += kFairyPollMs;
        }
        if (g_test_game_frame_ticks.load(std::memory_order_acquire) == 0) {
            return fail_fairy_run(state, "game never advanced a frame");
        }

        bool saw_fairy_alive = false;
        waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kFairyDeathTimeoutMs) {
            if (query_hired_fairy_life_state() == FairyLifeState::Alive) {
                saw_fairy_alive = true;
                break;
            }
            SDL_Delay(kFairyPollMs);
            waited_ms += kFairyPollMs;
        }

        if (!saw_fairy_alive) {
            return fail_fairy_run(state, "fairy never spawned alive in-world");
        }

        waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kFairyDeathTimeoutMs) {
            if (query_hired_fairy_life_state() != FairyLifeState::Alive) {
                state->observed_natural_death = true;
                break;
            }
            SDL_Delay(kFairyPollMs);
            waited_ms += kFairyPollMs;
        }

        if (g_test_in_game.load(std::memory_order_acquire) &&
            !state->observed_natural_death) {
            return fail_fairy_run(state, "fairy never died within timeout");
        }

        state->saw_generic_defeat =
            trace_contains("popup", "YOUR MEN ARE CRUSHED");

        if (g_test_in_game.load(std::memory_order_acquire)) {
            fprintf(stderr,
                    "  [test] fairy died, waiting for natural defeat to resolve\n");
            waited_ms = 0;
            while (g_test_in_game.load(std::memory_order_acquire) &&
                   waited_ms < kGameFinishTimeoutMs) {
                SDL_Delay(kFairyPollMs);
                waited_ms += kFairyPollMs;
            }
            if (g_test_in_game.load(std::memory_order_acquire)) {
                fprintf(stderr,
                        "  [test] natural defeat stalled; aborting after observed fairy death\n");
                inject_key_press(SDLK_ESCAPE);
                SDL_Delay(kUiSettleMs);
                picker_testing_yes_or_no_queue_clear();
                picker_testing_yes_or_no_queue_push(true);
                inject_key_press(SDLK_ESCAPE);

                waited_ms = 0;
                while (g_test_in_game.load(std::memory_order_acquire) &&
                       waited_ms < kGameAbortTimeoutMs) {
                    SDL_Delay(kFairyPollMs);
                    waited_ms += kFairyPollMs;
                }
                if (g_test_in_game.load(std::memory_order_acquire)) {
                    return fail_fairy_run(
                        state,
                        "game did not exit after aborting post-death stall");
                }
            }
        }

        fprintf(stderr, "  [test] game exited after fairy death path\n");
        state->saw_generic_defeat =
            trace_contains("popup", "YOUR MEN ARE CRUSHED");
        if (!state->observed_natural_death && !state->saw_generic_defeat) {
            return fail_fairy_run(state,
                                  "expected observed fairy death or generic defeat");
        }
        SDL_Delay(kMenuTransitionMs + kUiSettleMs);
        const int back_win_x = static_cast<int>(
            static_cast<float>(kTeamMenuBackGameX) *
                (og::runtime::current_session->viewport_w_ / 320.0f) +
            og::runtime::current_session->viewport_offset_x_);
        const int back_win_y = static_cast<int>(
            static_cast<float>(kTeamMenuBackGameY) *
                (og::runtime::current_session->viewport_h_ / 200.0f) +
            og::runtime::current_session->viewport_offset_y_);
        fprintf(stderr,
                "  [test] clicking back from team menu after auto-defeat\n");
        inject_click(back_win_x, back_win_y);
        SDL_Delay(kUiSettleMs);
        set_game_speed(state->original_speed);
        state->finished = true;
        return 0;
    }
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

    FairyState state = { false,
                         false,
                         false,
                         false,
                         og::runtime::current_session->g_game_speed_factor_,
                         nullptr };
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

    ASSERT_TRUE(state.finished)
        << (state.failure_message != nullptr
                ? state.failure_message
                : "injector thread should have completed");

    ASSERT_TRUE(state.observed_natural_death || state.saw_generic_defeat)
        << "the lone fairy run should observe a real fairy death or generic defeat";
    // We lost — level 4 should NOT be marked completed
    ASSERT_TRUE(!og::runtime::current_session->myscreen_->save_data.is_level_completed(4)) << "level 4 should NOT be completed (fairy should have died)";

    fprintf(stderr, "  [test] Fairy died as expected via UI hire flow\n");
}
