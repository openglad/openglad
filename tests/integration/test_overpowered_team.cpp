#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/gameplay/guy.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/save_data.h>
#include <openglad/core/util.h>
#include <openglad/interface/ui/pause_menu.h>
#include <openglad/interface/ui/picker_common.h>

#include <atomic>

// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
void picker_lobby_sync_roster_from_save();
void picker_lobby_sync_settings_from_save();

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {
constexpr Uint32 kUiSettleMs = 150;
constexpr int kTeamMenuTimeoutMs = 20000;
constexpr int kGameStartTimeoutMs = 20000;
constexpr int kGameFinishTimeoutMs = 90000;
constexpr int kGameAbortTimeoutMs = 20000;
constexpr int kMenuActionTimeoutMs = 5000;
}


#ifdef TESTING
extern bool g_test_remove_exits;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
#endif

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

static bool wait_for_team_menu(int timeout_ms = kTeamMenuTimeoutMs)
{
    int elapsed = 0;
    int stable_team_menu_polls = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        bool has_hire_troops = false;
        bool has_go = false;
        for (const std::string& id : get_button_ids()) {
            has_hire_troops = has_hire_troops || id == "hire_troops";
            has_go = has_go || id == "go";
        }
        if (has_hire_troops && has_go) {
            ++stable_team_menu_polls;
            if (stable_team_menu_polls >= 2)
                return true;
        } else {
            stable_team_menu_polls = 0;
        }

        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }

    fprintf(stderr, "  [interact] TIMEOUT entering team menu (%d ms)\n", timeout_ms);
    return false;
}

// Hire one of each character type via the actual UI, crank stats to absurd
// levels (programmatically), run level 1 at max speed, and confirm that we win.
//
// Flow:
//   Main Menu -> Begin New Game -> (accept company name) ->
//   Hire Menu -> cycle through all types, clicking HIRE ME for each ->
//   Back -> (cheat stats on hired team) -> GO -> game runs -> win ->
//   Back -> exits to main menu
//
// prompt_for_string() returns immediately under TESTING, accepting the
// default generated name for each hired character.

struct OpState {
    bool started;
    bool finished;
    float original_speed;
    int num_hired;
    const char* failure_message;
};

static int remaining_before(Uint64 deadline)
{
    const Uint64 now = SDL_GetTicks();
    return now < deadline ? static_cast<int>(deadline - now) : 0;
}

static bool wait_for_menu_click_release(Uint64 deadline)
{
    while (SDL_HasEvent(SDL_EVENT_MOUSE_BUTTON_UP)) {
        if (remaining_before(deadline) <= 0)
            return false;
        SDL_Delay(1);
    }

    // A menu action can publish its postcondition at the top of a frame
    // before that frame's leftmouse() call consumes the queued mouse-up.
    // Cross one more frame-top barrier before injecting the next press.
    const int remaining_ms = remaining_before(deadline);
    return remaining_ms > 0 &&
           run_on_main_thread([] {}, remaining_ms);
}

template <typename Predicate>
static bool interact_and_wait_for_menu_postcondition(
    const char* id,
    Predicate predicate,
    int timeout_ms = kMenuActionTimeoutMs)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    interact(id);

    while (remaining_before(deadline) > 0) {
        bool observed = false;
        const int remaining_ms = remaining_before(deadline);
        if (remaining_ms <= 0 ||
            !run_on_main_thread([&] { observed = predicate(); },
                                remaining_ms)) {
            break;
        }
        if (observed)
            return wait_for_menu_click_release(deadline);
    }

    fprintf(stderr,
            "  [test] ERROR: '%s' did not reach its menu postcondition "
            "within %d ms\n",
            id, timeout_ms);
    return false;
}

static void unwind_picker_after_failure(OpState* state)
{
    set_game_speed(state->original_speed);
    g_test_remove_exits = false;

    if (g_test_in_game.load(std::memory_order_acquire)) {
        og::ui::pause_menu_testing_clear_queue();
        og::ui::pause_menu_testing_queue_outcome(
            og::ui::PauseMenuResult::Quit, /*release_pause=*/false);
        inject_key_press(SDLK_ESCAPE);

        int waited_ms = 0;
        constexpr int poll_ms = 10;
        while (g_test_in_game.load(std::memory_order_acquire) &&
               waited_ms < kGameAbortTimeoutMs) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        og::ui::pause_menu_testing_clear_queue();
    }

    // infinite_gold is a session-only test aid. Reset it on the menu thread
    // before unwinding either Hire or Base Camp.
    (void)run_on_main_thread([] {
        og::runtime::current_session->myscreen_->save_data.infinite_gold = 0;
        picker_lobby_sync_settings_from_save();
    }, kMenuActionTimeoutMs);

    for (int step = 0; step < 2; ++step) {
        if (wait_for_interactable("begin_new_game", 1000))
            return;
        if (!wait_for_interactable("back", 2000))
            return;
        (void)wait_for_menu_click_release(
            SDL_GetTicks() + static_cast<Uint64>(kMenuActionTimeoutMs));
        interact("back");
    }
}

static int fail_op_run(OpState* state, const char* message)
{
    fprintf(stderr, "  [test] ERROR: %s\n", message);
    state->failure_message = message;
    unwind_picker_after_failure(state);
    return 0;
}

static int op_injector(void* data)
{
    og::runtime::ensure_thread_session();
    OpState* state = static_cast<OpState*>(data);
    state->started = true;

    // -- Main Menu --
    if (!wait_for_interactable("begin_new_game", 5000))
        return fail_op_run(state, "main menu did not appear");
    SDL_Delay(kUiSettleMs);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // §2.2: accept the generated company name at the name-entry screen.
    if (!accept_generated_company_name())
        return fail_op_run(state, "company-name screen did not appear");

    // No campaign intro here anymore (issue #186: it moved behind the
    // campaign select, skipped under TESTING) — an Escape here would BACK
    // out of the team-build screen instead.

    // New games now land on team build first, then enter hire explicitly.
    if (!wait_for_interactable("hire_troops", 10000))
        return fail_op_run(state, "team menu did not expose HIRE");
    if (!interact_and_wait_for_menu_postcondition("hire_troops", [] {
            const auto* const session = pks().hire_session;
            const guy* const recruit =
                session != nullptr ? session->current_recruit() : nullptr;
            return session != nullptr && session->family_index() == 0 &&
                   recruit != nullptr &&
                   recruit->family == og::ui::kAllowableGuys[0];
        })) {
        return fail_op_run(state, "hire menu did not start on SOLDIER");
    }

    // This test proves the complete HIRE/NEXT flow, not the random opening
    // wallet. Free purchases let every visible class exercise HIRE exactly
    // once; the session-only flag is restored before the level starts.
    if (!run_on_main_thread([] {
            og::runtime::current_session->myscreen_->save_data.infinite_gold = 1;
            picker_lobby_sync_settings_from_save();
        })) {
        return fail_op_run(state, "could not enable free test purchases");
    }

    // -- Hire Menu: hire one of each type --
    // The hire menu starts showing allowable_guys[0] (SOLDIER).
    // Each click is acknowledged by its exact roster/session postcondition,
    // then by the menu frame that consumes mouse-up. This keeps adjacent
    // HIRE/NEXT presses distinct even when the full binary is under load.
    fprintf(stderr, "  [test] hiring characters through UI...\n");
    for (std::size_t i = 0; i < og::ui::kAllowableGuys.size(); ++i) {
        const int expected_family = og::ui::kAllowableGuys[i];
        if (!interact_and_wait_for_menu_postcondition(
                "hire_me", [i, expected_family] {
                    const SaveData& save =
                        og::runtime::current_session->myscreen_->save_data;
                    const auto* const session = pks().hire_session;
                    return save.team_size == static_cast<int>(i + 1) &&
                           save.team_list[i] != nullptr &&
                           save.team_list[i]->family == expected_family &&
                           session != nullptr &&
                           session->family_index() == static_cast<int>(i);
                })) {
            return fail_op_run(state,
                               "HIRE did not add the displayed character");
        }

        if (i + 1 < og::ui::kAllowableGuys.size()) {
            const int next_family = og::ui::kAllowableGuys[i + 1];
            if (!interact_and_wait_for_menu_postcondition(
                    "next", [i, next_family] {
                        const auto* const session = pks().hire_session;
                        const guy* const recruit = session != nullptr
                            ? session->current_recruit()
                            : nullptr;
                        const guy* const displayed =
                            og::runtime::current_session->current_guy_.get();
                        return session != nullptr &&
                               session->family_index() ==
                                   static_cast<int>(i + 1) &&
                               recruit != nullptr &&
                               recruit->family == next_family &&
                               displayed != nullptr &&
                               displayed->family == next_family;
                    })) {
                return fail_op_run(
                    state, "NEXT did not display the next character type");
            }
        }
    }

    fprintf(stderr, "  [test] done hiring, clicking back\n");
    interact("back");

    // -- Team Menu: cheat stats then GO --
    if (!wait_for_team_menu())
        return fail_op_run(state, "team menu did not appear after hiring");
    if (!wait_for_menu_click_release(
            SDL_GetTicks() + static_cast<Uint64>(kMenuActionTimeoutMs))) {
        return fail_op_run(state, "Hire BACK release was not acknowledged");
    }

    // Programmatically crank every stat to ludicrous levels.
    // On the menu thread (#257): the team menu draws these roster fields
    // every frame.
    if (!run_on_main_thread([state] {
        SaveData& save =
            og::runtime::current_session->myscreen_->save_data;
        save.infinite_gold = 0;
        picker_lobby_sync_settings_from_save();
        state->num_hired = save.team_size;
        for (int i = 0; i < save.team_size; i++) {
            guy* g = save.team_list[static_cast<std::size_t>(i)].get();
            if (g) {
                g->strength = 200;
                g->dexterity = 200;
                g->constitution = 200;
                g->intelligence = 200;
                g->armor = 200;
            }
        }
        // Base Camp projects the lobby roster back into SaveData every
        // frame. Publish the cheated roster through the same boundary as a
        // real roster mutation so the next poll cannot restore stale stats.
        picker_lobby_sync_roster_from_save();
    })) {
        return fail_op_run(state, "could not prepare the hired roster");
    }

    bool roster_persisted = false;
    if (!run_on_main_thread([&] {
            const SaveData& save =
                og::runtime::current_session->myscreen_->save_data;
            roster_persisted =
                save.team_size ==
                    static_cast<int>(og::ui::kAllowableGuys.size());
            for (int i = 0; roster_persisted && i < save.team_size; ++i) {
                const guy* const g =
                    save.team_list[static_cast<std::size_t>(i)].get();
                roster_persisted =
                    g != nullptr && g->strength == 200 &&
                    g->dexterity == 200 && g->constitution == 200 &&
                    g->intelligence == 200 && g->armor == 200;
            }
        }) || !roster_persisted) {
        return fail_op_run(state,
                           "lobby projection did not retain cheated stats");
    }
    fprintf(stderr, "  [test] hired %d characters, cheating stats\n",
            state->num_hired);

    // Set up for auto-win: remove exits so level completes when enemies die
    g_test_remove_exits = true;
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
        while (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before
               && waited_ms < kGameStartTimeoutMs) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_game_epoch.load(std::memory_order_acquire) == epoch_before) {
            return fail_op_run(state,
                               "game never started (epoch unchanged)");
        }
        waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire)
               && waited_ms < kGameFinishTimeoutMs) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_in_game.load(std::memory_order_acquire)) {
            return fail_op_run(state,
                               "game did not finish within timeout");
        }
    }

    // Now we're truly back in create_team_menu with fresh buttons
    if (!wait_for_interactable("back", 10000))
        return fail_op_run(state, "Base Camp did not return after the game");

    // Restore state
    set_game_speed(state->original_speed);
    g_test_remove_exits = false;

    // Exit team menu -> main menu -> picker exits
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

TEST(OverpoweredTeam, overpowered_team) {
    trace_clear();

    // Start with empty team
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    OpState state = { false, false,
                      og::runtime::current_session->g_game_speed_factor_,
                      0, nullptr };
    SDL_Thread* thread = SDL_CreateThread(op_injector, "op_injector", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    // picker_main blocks — the injector thread drives all navigation
    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_EQ(nullptr, state.failure_message)
        << (state.failure_message != nullptr ? state.failure_message : "");
    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_EQ(static_cast<int>(og::ui::kAllowableGuys.size()), state.num_hired)
        << "should have hired exactly one character of every displayed type";
    ASSERT_EQ(0, og::runtime::current_session->myscreen_->save_data.infinite_gold)
        << "session-only free purchases must be restored before gameplay";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(1)) << "level 1 should be marked completed (team should have won)";

    fprintf(stderr, "  [test] Team of %d won level 1 via UI hire flow\n",
            state.num_hired);
}
