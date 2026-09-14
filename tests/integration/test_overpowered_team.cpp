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
#include "test_click_ladder.h"
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/match_stage.h>
#include <openglad/core/util.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {
// Poll ticks, not settles. Every screen transition in this file settles on
// wait_for_menu_frames(2) — all four screens the flow visits are
// run_menu_screen-hosted (menu_screen_specs.cpp) — and every click is proven
// consumed by the value it writes. These two are the tick of a wait-on-
// condition loop, which is why they are spelled with "poll"
// (scripts/check_injector_settles.sh, tier 2).
constexpr Uint32 kHirePollMs = 20;
constexpr Uint32 kEscapePollMs = 100;
// Cancellation ceilings for a pump that has stopped, never budgets. Do not
// raise them to buy time on an instrumented lane: a lane that needs more than
// these has stopped pumping, and the escape tail below is what turns that
// into a named failure instead of a wedged binary.
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

// The team-build screen is up once BOTH of its doors are published. Two
// consecutive positive polls, because create_team_menu rebuilds its button
// list in place and a single poll can catch it half-built.
//
// The two polls are the condition, never a clock: a caller that asks for a
// 1 ms window — click_until_edge's re-check before a re-press
// (tests/test_click_ladder.h, kEdgeRecheckMs) — still gets both, so a `back`
// that landed late is FOUND there and the re-press is cancelled. That matters
// here more than anywhere else in the file: `back` is published by the hire
// screen and by the team screen alike, so a blind re-press would leave the
// team screen the flow just entered.
static bool wait_for_team_menu(int timeout_ms = kTeamMenuTimeoutMs)
{
    int elapsed = 0;
    int stable_team_menu_polls = 0;
    const int poll_interval = 50;
    while (true) {
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
            if (elapsed >= timeout_ms)
                break;
            stable_team_menu_polls = 0;
        }

        SDL_Delay(static_cast<Uint32>(poll_interval));
        elapsed += poll_interval;
    }

    fprintf(stderr, "  [interact] TIMEOUT entering team menu (%d ms)\n", timeout_ms);
    return false;
}

static bool click_hire_and_wait_for_roster_growth(int timeout_ms = 5000)
{
    short team_size_before = -1;
    if (!run_on_main_thread([&team_size_before] {
            team_size_before =
                og::runtime::current_session->myscreen_->save_data.team_size;
        })) {
        return false;
    }

    interact("hire_me");
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        bool grew = false;
        if (!run_on_main_thread([team_size_before, &grew] {
                grew = og::runtime::current_session->myscreen_->save_data.team_size >
                    team_size_before;
                if (grew)
                    reset_mouse_click_tracking();
            })) {
            return false;
        }
        if (grew)
            return true;
        SDL_Delay(kHirePollMs);
        elapsed += static_cast<int>(kHirePollMs);
    }
    return false;
}

static bool click_next_and_wait_for_family_change(int timeout_ms = 5000)
{
    char family_before = 0;
    bool had_current_guy = false;
    if (!run_on_main_thread([&family_before, &had_current_guy] {
            const guy* const current =
                og::runtime::current_session->current_guy_.get();
            had_current_guy = current != nullptr;
            if (current != nullptr)
                family_before = current->family;
        }) || !had_current_guy) {
        return false;
    }

    interact("next");
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        bool changed = false;
        if (!run_on_main_thread([family_before, &changed] {
                const guy* const current =
                    og::runtime::current_session->current_guy_.get();
                changed = current != nullptr && current->family != family_before;
                if (changed)
                    reset_mouse_click_tracking();
            })) {
            return false;
        }
        if (changed)
            return true;
        SDL_Delay(kHirePollMs);
        elapsed += static_cast<int>(kHirePollMs);
    }
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
//
// Every leg of that flow is NUMBERED, and every give-up routes through the
// escape tail below instead of returning into a wedged binary: picker_main
// blocks on the main thread and only a click can make it return, so an
// injector that gave up silently used to take the whole og_test_game_core
// binary to the 420 s CTest ceiling (observed once on the coverage lane).

struct OpState {
    bool started = false;
    bool finished = false;
    float original_speed = 0.0f;
    int num_hired = 0;
    // Points at failure_detail once a leg gives up; nullptr on the happy path.
    const char* failure_message = nullptr;
    std::string failure_detail;
    // Set by the MAIN thread after picker_main returns. The escape tail has no
    // wall-clock bound, because the main thread cannot leave the blocking menu
    // on its own: a tail that stopped trying early would GUARANTEE the wedge it
    // exists to prevent.
    std::atomic<bool> test_finished{false};
    // Point one leg at an id the flow never publishes, so the give-up path
    // itself is testable (the shape AddCycleFlow and the pause-menu flow carry).
    int sabotage_leg = 0;
};

static int op_injector(void* data)
{
    og::runtime::ensure_thread_session();
    OpState* state = static_cast<OpState*>(data);
    state->started = true;

    // The escape tail. Restore what the flow changed, then keep closing
    // whatever screen is currently published until the main thread says it is
    // out of picker_main for good, and report the leg that gave up.
    //
    // "Closing" the main menu means going FORWARD: mainmenu() blocks until the
    // player picks something, and with g_picker_max_mainmenu_calls == 1 the
    // second visit to the main menu is answered with QUIT without drawing, so
    // begin_new_game -> accept name -> team screen -> back is the way OUT.
    // The happy path ends in this same loop (escape(0)), so the final `back`
    // — after which picker_main returns and no pump exists to service a
    // ladder's acknowledge post — is driven by one exit rule, not two.
    const auto escape = [state](int leg, const std::string& why) {
        if (leg != 0) {
            fprintf(stderr, "  [test] ERROR: leg %d: %s\n", leg, why.c_str());
            state->failure_detail = why;
            state->failure_message = state->failure_detail.c_str();
        }
        set_game_speed(state->original_speed);
        g_test_remove_exits = false;
        while (!state->test_finished.load()) {
            if (g_test_in_game.load(std::memory_order_acquire)) {
                SDL_Delay(kEscapePollMs);
                continue;
            }
            if (has_interactable("hire_me"))
                (void)interact("back");
            else if (has_interactable("go"))
                (void)interact("back");
            else if (has_interactable("company_name_accept"))
                (void)interact("company_name_accept");
            else if (has_interactable("begin_new_game"))
                (void)interact("begin_new_game");
            SDL_Delay(kEscapePollMs);
        }
        return leg;
    };

    // -- Leg 1: Main Menu --
    // The sabotaged run points this wait at an id the main menu never
    // publishes. Its window is short on purpose: nothing is ever coming, and
    // the point of that run is the tail, not the wait.
    const char* const start_id =
        state->sabotage_leg == 1 ? "op_never_published" : "begin_new_game";
    const int start_wait_ms = state->sabotage_leg == 1 ? 500 : 5000;
    if (!wait_for_interactable(start_id, start_wait_ms) ||
        !wait_for_menu_frames(2))
        return escape(1, "the main menu never published begin_new_game");

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    if (!interact("begin_new_game"))
        return escape(1, "begin_new_game vanished before the click");

    // -- Leg 2: §2.2 accept the generated company name --
    // No campaign intro here anymore (issue #186: it moved behind the
    // campaign select, skipped under TESTING) — an Escape here would BACK
    // out of the team-build screen instead.
    if (!accept_generated_company_name())
        return escape(2, "the generated company name was never accepted");

    // -- Leg 3: the hire door --
    // New games now land on team build first, then enter hire explicitly.
    // The door is pressed through the ladder: an evaporated press here used
    // to cost a 10 s ceiling and then a wedge.
    if (!wait_for_interactable("hire_troops", kTeamMenuTimeoutMs) ||
        !wait_for_menu_frames(2) ||
        !click_until_edge("hire_troops",
                          [](int wait_ms) {
                              return wait_for_interactable("hire_me", wait_ms);
                          }) ||
        !wait_for_menu_frames(2))
        return escape(3, "the HIRE TROOPS door never opened the hire screen");

    // -- Hire Menu: hire one of each type (legs 4 and 5) --
    // The hire menu starts showing allowable_guys[0] (SOLDIER).
    // For each type: click HIRE ME (succeeds if we can afford it),
    // then click NEXT to cycle to the next type. Each click is proven
    // consumed by the roster growth / family change it causes, polled at
    // kHirePollMs.
    fprintf(stderr, "  [test] hiring characters through UI...\n");
    for (int i = 0; i < NUM_HIRE_TYPES; i++) {
        if (!click_hire_and_wait_for_roster_growth())
            return escape(4, "hire click " + std::to_string(i) +
                                 " did not grow the roster");

        if (i < NUM_HIRE_TYPES - 1) {
            if (!click_next_and_wait_for_family_change())
                return escape(5, "next click " + std::to_string(i) +
                                     " did not change the hire family");
        }
    }

    // -- Leg 6: back to the team screen --
    // Every click above is acknowledged on the menu thread after its roster or
    // family edge. BACK therefore starts from a clean pointer baseline even
    // when autosave work makes one menu frame unusually long. The ladder's
    // edge is the TEAM screen itself, not the button id: `back` is published
    // by both screens, so only the arriving screen proves which one consumed
    // the press.
    fprintf(stderr, "  [test] done hiring, clicking back\n");
    if (!click_until_edge("back",
                          [](int wait_ms) {
                              return wait_for_team_menu(wait_ms);
                          }) ||
        !wait_for_menu_frames(2))
        return escape(6, "BACK from the hire screen never reached team build");

    // -- Team Menu: cheat stats then GO --
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
        return escape(7, "roster preparation never reached the menu thread");
    if (!roster_configured)
        return escape(8, "the authoritative stage did not retain the setup");
    fprintf(stderr, "  [test] hired %d characters, cheating stats\n",
            state->num_hired);

    // Set up for auto-win: remove exits so level completes when enemies die
    g_test_remove_exits = true;
    set_game_speed(0.0f);

    // -- Leg 9: GO --
    // go_menu dispatches synchronously on the menu thread (button.cpp ->
    // go_menu -> picker_testing_mark_game_start), so a bumped epoch is an
    // exact landing witness for this press. attempts=1: a second landed GO
    // would launch a second battle, which is exactly the toggle-safety case
    // the ladder refuses to risk.
    fprintf(stderr, "  [test] clicking go\n");
    const int epoch_before = g_test_game_epoch.load(std::memory_order_acquire);
    const auto game_epoch_advanced = [epoch_before](int wait_ms) {
        int waited_ms = 0;
        const int poll_ms = 50;
        while (waited_ms < wait_ms) {
            if (g_test_game_epoch.load(std::memory_order_acquire) !=
                epoch_before)
                return true;
            SDL_Delay(static_cast<Uint32>(poll_ms));
            waited_ms += poll_ms;
        }
        return false;
    };
    if (!click_until_edge("go", game_epoch_advanced, /*landed_trace=*/nullptr,
                          /*attempts=*/1, /*wait_ms=*/kGameStartTimeoutMs))
        return escape(9, "GO never started the game (epoch unchanged)");

    // -- Leg 10: wait for glad_main to finish --
    // Old buttons from create_team_menu persist through glad_main, so
    // wait_for_interactable("back") would return immediately (stale buttons);
    // g_test_in_game is set and cleared around glad_main itself.
    fprintf(stderr, "  [test] waiting for game to finish...\n");
    {
        int waited_ms = 0;
        const int poll_ms = 50;
        while (g_test_in_game.load(std::memory_order_acquire)
               && waited_ms < kGameFinishTimeoutMs) {
            SDL_Delay(static_cast<Uint32>(poll_ms));
            waited_ms += poll_ms;
        }
        if (g_test_in_game.load(std::memory_order_acquire))
            return escape(10, "the game did not finish within its ceiling");
    }

    // -- Leg 11: back in create_team_menu with fresh buttons --
    if (!wait_for_team_menu() || !wait_for_menu_frames(2))
        return escape(11, "the team menu never came back after the game");

    fprintf(stderr, "  [test] clicking back from team menu\n");
    state->finished = true;
    return escape(0, "");
}

// The save setup both flows below found their company from: an empty roster,
// one player, the gladiator campaign, no injected squads, infinite gold.
static void reset_save_for_op_run()
{
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
    // Hiring every family is the UI path under test; random recruit stats
    // must not make the later families unaffordable in only some runs.
    save.infinite_gold = 1;
    ASSERT_TRUE(save.save("save0"));
}

TEST(OverpoweredTeam, overpowered_team) {
    trace_clear();
    const ScopedMatchSeed match_seed(kBattleSeed);
    ASSERT_EQ(kBattleSeed, og::server::draw_match_seed());

    ASSERT_NO_FATAL_FAILURE(reset_save_for_op_run());

    OpState state;
    state.original_speed =
        og::runtime::current_session->g_game_speed_factor_;
    SDL_Thread* thread = SDL_CreateThread(op_injector, "op_injector", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    // picker_main blocks — the injector thread drives all navigation
    picker_main(0, nullptr);

    // The tail is running until it is told the menus are gone for good.
    state.test_finished.store(true);
    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    // The tail's last click may have been pushed after picker_main returned
    // under it; a stray mouse event must not ride into the next test's menu.
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_EQ(nullptr, state.failure_message)
        << "injector setup failed: " << state.failure_message;
    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    EXPECT_EQ(NUM_HIRE_TYPES, state.num_hired)
        << "every hireable family must have been hired through the UI";
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.is_level_completed(1)) << "level 1 should be marked completed (team should have won)";

    fprintf(stderr, "  [test] Team of %d won level 1 via UI hire flow\n",
            state.num_hired);
}

// The regression for the wedge itself: a leg that gives up inside the
// blocking team-build flow must free the main thread and report its number.
// Leg 1 is pointed at an id the main menu never publishes; the escape tail
// then has to walk the founding flow all the way to a screen that can be
// backed out of, so picker_main returns and SDL_WaitThread joins.
//
// Replace `return escape(1, ...)` with a bare `return 1` (the shape this file
// carried before PR #292) and this test hangs until the CTest ceiling.
TEST(OverpoweredTeam, a_leg_that_gives_up_frees_the_main_thread) {
    trace_clear();
    const ScopedMatchSeed match_seed(kBattleSeed);

    ASSERT_NO_FATAL_FAILURE(reset_save_for_op_run());

    const float speed_before =
        og::runtime::current_session->g_game_speed_factor_;
    OpState state;
    state.original_speed = speed_before;
    state.sabotage_leg = 1;
    SDL_Thread* thread =
        SDL_CreateThread(op_injector, "op_escape_injector", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    state.test_finished.store(true);
    int injector_result = -1;
    SDL_WaitThread(thread, &injector_result);
    SDL_PumpEvents();
    SDL_FlushEvents(SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_WHEEL);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(state.started) << "the injector thread never ran";
    EXPECT_EQ(1, injector_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_FALSE(state.finished)
        << "a flow that gave up at leg 1 never completed";
    EXPECT_NE(nullptr, state.failure_message)
        << "the give-up must name what it was waiting for";
    EXPECT_FALSE(og::runtime::current_session->myscreen_->save_data
                     .is_level_completed(1))
        << "no battle was ever fought, so level 1 cannot be complete";
    EXPECT_EQ(speed_before, og::runtime::current_session->g_game_speed_factor_)
        << "the tail restores the game speed it found";
    EXPECT_FALSE(g_test_remove_exits)
        << "the tail restores the exit-removal cheat it found";
}
