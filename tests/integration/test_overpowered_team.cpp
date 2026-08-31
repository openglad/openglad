#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>
#include <openglad/core/util.h>

#include <atomic>
#include <cstdint>
#include <optional>

// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {
constexpr Uint32 kUiSettleMs = 150;
constexpr Uint32 kMenuTransitionMs = 250;
constexpr Uint32 kCycleStepMs = 100;
constexpr int kTeamMenuTimeoutMs = 20000;
constexpr int kGameStartTimeoutMs = 20000;
constexpr int kGameFinishTimeoutMs = 90000;
// Real staged lobbies draw non-simulation entropy once per round. Pin this
// battle so the victory oracle exercises one reproducible fight instead of a
// random_device/clock-selected trajectory.
constexpr std::uint32_t kBattleSeed = 0x0F00A90Eu;

class ScopedMatchSeed final
{
public:
    explicit ScopedMatchSeed(std::uint32_t seed)
    {
        og::server::set_match_seed_for_testing(seed);
    }

    ~ScopedMatchSeed()
    {
        og::server::set_match_seed_for_testing(std::nullopt);
    }

    ScopedMatchSeed(const ScopedMatchSeed&) = delete;
    ScopedMatchSeed& operator=(const ScopedMatchSeed&) = delete;
};
}


#ifdef TESTING
extern bool g_test_remove_exits;
extern std::atomic<bool> g_test_in_game;
extern std::atomic<int> g_test_game_epoch;
#endif

// Number of hireable character types in allowable_guys[]
#define NUM_HIRE_TYPES 14

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

static int fail_op_run(OpState* state, const char* message)
{
    fprintf(stderr, "  [test] ERROR: %s\n", message);
    state->failure_message = message;
    set_game_speed(state->original_speed);
    g_test_remove_exits = false;

    // Leave the team screen so picker_main can return and the owning test can
    // report the setup failure instead of hanging in SDL_WaitThread.
    if (wait_for_interactable("back", 2000)) {
        SDL_Delay(kUiSettleMs);
        interact("back");
    }
    return 0;
}

static int op_injector(void* data)
{
    og::runtime::ensure_thread_session();
    OpState* state = static_cast<OpState*>(data);
    state->started = true;

    // -- Main Menu --
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(kUiSettleMs);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // §2.2: accept the generated company name at the name-entry screen.
    accept_generated_company_name();

    // No campaign intro here anymore (issue #186: it moved behind the
    // campaign select, skipped under TESTING) — an Escape here would BACK
    // out of the team-build screen instead.

    // New games now land on team build first, then enter hire explicitly.
    SDL_Delay(kUiSettleMs);
    wait_for_interactable("hire_troops", 10000);
    SDL_Delay(kUiSettleMs);
    interact("hire_troops");
    SDL_Delay(kUiSettleMs);
    wait_for_interactable("hire_me", 10000);
    SDL_Delay(kUiSettleMs);

    // -- Hire Menu: hire one of each type --
    // The hire menu starts showing allowable_guys[0] (SOLDIER).
    // For each type: click HIRE ME (succeeds if we can afford it),
    // then click NEXT to cycle to the next type.
    fprintf(stderr, "  [test] hiring characters through UI...\n");
    for (int i = 0; i < NUM_HIRE_TYPES; i++) {
        interact("hire_me");
        SDL_Delay(kCycleStepMs);

        if (i < NUM_HIRE_TYPES - 1) {
            interact("next");
            SDL_Delay(kCycleStepMs);
        }
    }

    // The final roster update becomes visible before add_guy's main-thread
    // callback finishes its input reset, recruit sync, and autosave tail.
    // Do not let BACK overlap that callback.
    SDL_Delay(750);
    fprintf(stderr, "  [test] done hiring, clicking back\n");
    interact("back");

    // -- Team Menu: cheat stats then GO --
    SDL_Delay(kUiSettleMs);
    if (!wait_for_team_menu()) {
        set_game_speed(state->original_speed);
        g_test_remove_exits = false;
        return 0;
    }
    SDL_Delay(kUiSettleMs);

    // Programmatically crank every stat to ludicrous levels
    // On the menu thread (#257): the team menu draws these roster fields
    // every frame.
    bool roster_configured = false;
    if (!run_on_main_thread([state, &roster_configured] {
        SaveData& save =
            og::runtime::current_session->myscreen_->save_data;
        state->num_hired = save.team_size;
        for (auto& member : save.team_list) {
            if (!member)
                continue;
            member->strength = 200;
            member->dexterity = 200;
            member->constitution = 200;
            member->intelligence = 200;
            member->armor = 200;
        }

        // The lobby is authoritative and the next menu-frame poll rebuilds
        // team_list from it. Publish the cheated roster, drive the stage from
        // that authoritative state on this same main-thread turn, then prove
        // both the rebuilt save and the world GO will adopt carry the setup.
        picker_lobby_sync_roster_from_save();
        picker_lobby_poll();

        const auto roster_is_overpowered = [state](const SaveData& roster) {
            int verified_members = 0;
            for (const auto& member : roster.team_list) {
                if (!member)
                    continue;
                ++verified_members;
                if (member->strength != 200 || member->dexterity != 200 ||
                    member->constitution != 200 ||
                    member->intelligence != 200 || member->armor != 200) {
                    return false;
                }
            }
            return roster.team_size == state->num_hired &&
                verified_members == state->num_hired;
        };
        const auto lineup_is_baseline = [](const SaveData& roster) {
            for (std::size_t team = 0; team < roster.fill.size(); ++team) {
                if (roster.fill[team] != og::sim::kFillNone ||
                    roster.map_units[team] != og::sim::kMapUnitsOn) {
                    return false;
                }
            }
            return true;
        };

        og::ui::IPickerLobbyClient* const lobby =
            og::ui::active_picker_lobby_client();
        og::server::MatchStage* const stage =
            lobby != nullptr ? lobby->take_match_stage() : nullptr;
        if (stage == nullptr || stage->match_seed() != kBattleSeed ||
            !stage->ensure_current(og::server::stage_clock_now_ms())) {
            return;
        }
        roster_configured = roster_is_overpowered(save) &&
            roster_is_overpowered(stage->staged_save()) &&
            lineup_is_baseline(save) &&
            lineup_is_baseline(stage->staged_save());
    }))
        return fail_op_run(
            state, "roster preparation never reached the menu thread");
    if (!roster_configured)
        return fail_op_run(
            state, "authoritative stage did not retain roster/rule setup");
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
            fprintf(stderr, "  [test] ERROR: game never started (epoch unchanged)\n");
            set_game_speed(state->original_speed);
            g_test_remove_exits = false;
            return 0;
        }
        waited_ms = 0;
        while (g_test_in_game.load(std::memory_order_acquire)
               && waited_ms < kGameFinishTimeoutMs) {
            SDL_Delay(poll_ms);
            waited_ms += poll_ms;
        }
        if (g_test_in_game.load(std::memory_order_acquire)) {
            fprintf(stderr, "  [test] ERROR: game did not finish within timeout\n");
            set_game_speed(state->original_speed);
            g_test_remove_exits = false;
            return 0;
        }
    }

    // Now we're truly back in create_team_menu with fresh buttons
    wait_for_interactable("back", 10000);
    SDL_Delay(kUiSettleMs);

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
    const ScopedMatchSeed match_seed(kBattleSeed);
    ASSERT_EQ(kBattleSeed, og::server::draw_match_seed());

    // Start with empty team
    SaveData& save =
        og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    // Company resets preserve lobby match settings by design. This test's
    // oracle assumes only the authored level-1 armies: no injected squads,
    // with every map-authored unit still enabled.
    save.fill.fill(og::sim::kFillNone);
    save.map_units.fill(og::sim::kMapUnitsOn);
    ASSERT_TRUE(save.save("save0"));

    OpState state = {
        false,
        false,
        og::runtime::current_session->g_game_speed_factor_,
        0,
        nullptr,
    };
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
        << "injector setup failed: " << state.failure_message;
    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.num_hired >= 5) << "should have hired at least 5 characters via UI";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(1)) << "level 1 should be marked completed (team should have won)";

    fprintf(stderr, "  [test] Team of %d won level 1 via UI hire flow\n",
            state.num_hired);
}
