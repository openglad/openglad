#include <memory>
#include <array>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/interface/button.h>
#include "../../src/interface/ui/picker_sdl_defs.h"
#include <openglad/core/test_trace.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include <openglad/resources/company.h>
#include <openglad/resources/og_file.h>
#include <openglad/resources/save_data.h>
#include <openglad/gameplay/guy.h>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
Sint32 beginmenu(Sint32 arg1);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {
constexpr int kTeamMenuTimeoutMs = 20000;
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

static bool wait_for_team_menu(int timeout_ms = kTeamMenuTimeoutMs)
{
    int elapsed = 0;
    const int poll_interval = 50;
    while (elapsed < timeout_ms) {
        if (has_interactable("hire_troops") && has_interactable("networking"))
            return true;

        SDL_Delay(poll_interval);
        elapsed += poll_interval;
    }

    fprintf(stderr, "  [interact] TIMEOUT entering team menu (%d ms)\n",
            timeout_ms);
    return false;
}

// Test: Click "BEGIN NEW GAME" from the main menu, which should reset save data
// and land directly on the team-build screen with Networking available.
// Then click BACK to return to the main menu.
//
// This verifies:
//   1. The begin_new_game button works
//   2. Save data gets reset on new game
//   3. The team-build menu appears immediately after founding
//   4. Networking is available from that menu
//   5. Navigation back to main menu works

struct NewGameState {
    bool started;
    bool finished;
    bool saw_team_menu;
    bool saw_networking_button;
    // #237 derivation pin: fades counted across the BEGIN NEW GAME door.
    // -1 means the injector never reached the read.
    int fades_added_by_name_entry;
    // #237 symmetry leg: fades counted across the name-entry BACK, from the
    // cancel click to the re-presented main menu. -1 = never read.
    int fades_added_by_name_entry_back;
};

// Under TESTING every fadeblack takes FadeBetween's test-mode branch, which
// traces exactly one "video" line per fade — so counting those lines counts
// fades. The #237 rule is derived inside run_menu_screen rather than declared
// on a spec, so only driving the real door proves which side a screen is on.
static int count_fade_between_traces()
{
    std::lock_guard<std::mutex> lock(g_trace_mutex);
    int fades = 0;
    for (const TraceEntry& entry : g_trace_buffer) {
        if (entry.category == "video" &&
            entry.message.find("FadeBetween") != std::string::npos)
            ++fades;
    }
    return fades;
}

static int new_game_injector(void* data)
{
    og::runtime::ensure_thread_session();
    NewGameState* state = static_cast<NewGameState*>(data);
    state->started = true;

    // Wait for main menu
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    // #237: the new-game cut is a context switch, and name entry is its first
    // step — so its entry fades, unlike the doors that stay inside the menu
    // cluster.
    const int fades_before_new_game = count_fade_between_traces();
    interact("begin_new_game");

    // §2.2: BEGIN NEW GAME now opens the name-entry screen first. Accept the
    // generated company name to found the company.
    wait_for_interactable("company_name_accept", 5000);
    SDL_Delay(750);  // menu-entry settle
    state->fades_added_by_name_entry =
        count_fade_between_traces() - fades_before_new_game;
    fprintf(stderr, "  [test] accepting generated company name\n");
    interact("company_name_accept");

    // The campaign intro no longer runs inside picker_prepare_new_game_setup
    // (issue #186: it moved behind the campaign select). Under TESTING the
    // browser auto-accepts the current campaign and the intro scroller
    // dismisses itself, each after one real presented frame, so the flow
    // proceeds to the team-build menu without further input (the per-leg
    // fades of those two screens are pinned in test_fade_ownership.cpp).
    SDL_Delay(500);
    if (wait_for_team_menu()) {
        state->saw_team_menu = true;
        state->saw_networking_button = has_interactable("networking");
        SDL_Delay(750);

        // Click BACK to return to main menu
        fprintf(stderr, "  [test] clicking back from team menu\n");
        interact("back");
    }

    state->finished = true;
    return 0;
}

TEST(NewGame, begin_new_game) {
    trace_clear();

    // Pre-populate save data so we can verify it gets reset
    og::runtime::current_session->myscreen_->save_data.totalcash = 99999;
    og::runtime::current_session->myscreen_->save_data.totalscore = 55555;
    og::runtime::current_session->myscreen_->save_data.scen_num = 5;
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    // §2.1: BEGIN NEW GAME's "There is already a game loaded. Do you want to
    // restart?" prompt is RETIRED — founding a company never destroys the
    // loaded game. Seed save0 with a team member (the exact team_size > 0
    // condition that used to raise the prompt) so this flow proves BEGIN NEW
    // GAME now founds without asking. Were the prompt still present, the
    // injector — which never answers it — would hang until timeout.
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        og::runtime::current_session->myscreen_->save_data.team_list[static_cast<std::size_t>(i)].reset(nullptr);
    }
    og::runtime::current_session->myscreen_->save_data.team_list[0] =
        std::make_unique<guy>(FAMILY_SOLDIER);
    og::runtime::current_session->myscreen_->save_data.team_size = 1;
    og::runtime::current_session->myscreen_->save_data.save("save0");

    NewGameState state = { false, false, false, false, -1, -1 };
    SDL_Thread* thread = SDL_CreateThread(new_game_injector, "new_game_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_team_menu) << "should have landed on the team menu after new game";
    ASSERT_TRUE(state.saw_networking_button) << "networking should be available immediately after new game";

    // beginmenu calls save_data.reset(), so cash should be the default (starting cash)
    // rather than our 99999
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.totalcash != 99999) << "totalcash should have been reset by new game";

    // §2.2: the name-entry ACCEPT founded the company (traced with the chosen
    // display name), and that name landed in the 40-byte save_name field.
    // #237, derived rather than declared: BEGIN NEW GAME is a context switch
    // and name entry is its first step, so the entry fades out and back in
    // exactly once. (Contrast the LOAD / BACKUPS doors, pinned at 0 in
    // test_company_list.)
    EXPECT_EQ(2, state.fades_added_by_name_entry)
        << "#237: the name-entry screen is the new-game context switch — its "
           "entry must fade out and in exactly once";
    ASSERT_TRUE(trace_contains("name_entry", "accept")) << "name-entry ACCEPT should have fired";
    ASSERT_FALSE(og::runtime::current_session->myscreen_->save_data.save_name.empty())
        << "the founded company's display name should be stamped into save_name";
}

// §2.2: BACK from the name-entry screen founds NOTHING — the previously loaded
// game survives (its cash is not reset), and REROLL is reachable/clickable
// before cancelling. Proves the "nothing is destroyed" contract for cancel.
static int name_entry_cancel_injector(void* data)
{
    og::runtime::ensure_thread_session();
    NewGameState* state = static_cast<NewGameState*>(data);
    state->started = true;

    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // Name-entry appears. Reroll the suggestion, then BACK out (cancel).
    if (wait_for_interactable("company_name_reroll", 5000)) {
        SDL_Delay(750);  // menu-entry settle
        fprintf(stderr, "  [test] clicking REROLL\n");
        interact("company_name_reroll");
        SDL_Delay(300);  // let the click release before the next press
        fprintf(stderr, "  [test] clicking BACK (cancel)\n");
        // #237 symmetry: the way back out of a main-menu door owes exactly
        // what the way in cost — the door fades out, the re-entered main menu
        // fades in.
        const int fades_before_back = count_fade_between_traces();
        interact("back");
        state->saw_team_menu = true;  // reused flag: reached & left name-entry
        // The re-entered main menu is a SECOND mainmenu call, so this flow
        // runs with g_picker_max_mainmenu_calls = 2 and leaves through QUIT.
        if (wait_for_interactable("begin_new_game", 5000)) {
            SDL_Delay(750);  // menu-entry settle
            state->fades_added_by_name_entry_back =
                count_fade_between_traces() - fades_before_back;
            fprintf(stderr, "  [test] quitting from the main menu\n");
            interact("quit");
        }
    }

    state->finished = true;
    return 0;
}

TEST(NewGame, name_entry_back_cancels_without_founding) {
    trace_clear();

    // Seed a distinctly-valued loaded game and persist it: picker_main reloads
    // the active company (save0) at startup, so the sentinels must be on disk
    // to survive into the run. BACK must then leave them untouched.
    og::runtime::current_session->myscreen_->save_data.totalcash = 424242;
    og::runtime::current_session->myscreen_->save_data.save_name = "PRIOR COMPANY";
    og::runtime::current_session->myscreen_->save_data.current_campaign =
        "gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    NewGameState state = { false, false, false, false, -1, -1 };
    SDL_Thread* thread =
        SDL_CreateThread(name_entry_cancel_injector, "name_entry_cancel", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    // 2: the door's way in is one mainmenu call, the way back re-enters it —
    // and the #237 return-leg pin can only be measured on a main menu that
    // actually re-runs (the cap short-circuits present_menu straight to QUIT).
    g_picker_max_mainmenu_calls = 2;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_team_menu) << "should have reached the name-entry screen";
    ASSERT_TRUE(trace_contains("name_entry", "reroll")) << "REROLL should have fired";
    ASSERT_TRUE(trace_contains("name_entry", "cancel")) << "BACK should have cancelled";
    EXPECT_EQ(2, state.fades_added_by_name_entry_back)
        << "#237 symmetry: cancelling name entry returns to the main menu — "
           "the way back must fade exactly as much as the way in (2)";
    // The loaded game survived: cancel founded nothing, so no reset ran.
    ASSERT_EQ(424242u, og::runtime::current_session->myscreen_->save_data.totalcash)
        << "cancel must not reset the loaded game";
    ASSERT_EQ("PRIOR COMPANY", og::runtime::current_session->myscreen_->save_data.save_name)
        << "cancel must not overwrite the loaded company name";
}

static int direct_beginmenu_cancel_injector(void*)
{
    og::runtime::ensure_thread_session();
    if (!wait_for_interactable("company_name_reroll", 5000))
        return 1;
    SDL_Delay(250);
    interact("back");
    return 0;
}

TEST(NewGame, beginmenu_propagates_name_entry_cancel_without_resetting_save)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const auto saved_cash = save.totalcash;
    const std::string saved_name = save.save_name;

    SDL_Thread* thread = SDL_CreateThread(
        direct_beginmenu_cancel_injector, "direct_beginmenu_cancel", nullptr);
    ASSERT_TRUE(thread != nullptr);
    EXPECT_EQ(MENU_REDRAW, beginmenu(99));
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    EXPECT_EQ(0, thread_result);
    EXPECT_EQ(saved_cash, save.totalcash);
    EXPECT_EQ(saved_name, save.save_name);
}

// §2.2: clicking the name strip opens an in-place editor; the typed name
// becomes the founded company's display name. Exercises the SDL edit path
// (input_string_value under the engine's MenuSpecRow dispatch).
static int name_entry_edit_injector(void* data)
{
    og::runtime::ensure_thread_session();
    NewGameState* state = static_cast<NewGameState*>(data);
    state->started = true;

    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    if (wait_for_interactable("company_name_value", 5000)) {
        SDL_Delay(750);  // menu-entry settle
        fprintf(stderr, "  [test] clicking the name strip to edit\n");
        interact("company_name_value");  // opens input_string_value (blocks)
        SDL_Delay(400);  // let the engine dispatch + the editor start + clear
        // The first text input replaces the pre-filled suggestion entirely.
        inject_text_input("MY GUILD");
        SDL_Delay(50);
        inject_key_press(SDLK_RETURN);  // commit the edit
        SDL_Delay(400);
        fprintf(stderr, "  [test] accepting the edited name\n");
        interact("company_name_accept");
    }

    // The campaign select and intro run un-driven under TESTING (auto-accept
    // and auto-dismiss after one presented frame each) — the flow reaches
    // team build on its own. Unwind back to the main menu so picker_main can
    // hit its Quit gate.
    SDL_Delay(500);
    if (wait_for_team_menu()) {
        state->saw_team_menu = true;
        SDL_Delay(750);
        fprintf(stderr, "  [test] clicking back from team menu\n");
        interact("back");
    }

    state->finished = true;
    return 0;
}

TEST(NewGame, name_entry_edit_strip_sets_company_name) {
    trace_clear();

    NewGameState state = { false, false, false, false, -1, -1 };
    SDL_Thread* thread =
        SDL_CreateThread(name_entry_edit_injector, "name_entry_edit", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_team_menu) << "should have reached the name-entry strip";
    ASSERT_TRUE(trace_contains("name_entry", "edit MY GUILD"))
        << "the strip edit should capture the typed name";
    ASSERT_EQ("MY GUILD", og::runtime::current_session->myscreen_->save_data.save_name)
        << "the edited name should become the founded company's display name";
}

// Regression for the originally reported flow:
//   BEGIN NEW GAME -> Base Camp BACK -> PLAYERS -> 3 PLAYER -> BACK
//   -> CONTINUE -> Base Camp.
//
// PLAYERS has since moved into Base Camp as the seat rail's + action. Keep
// the heart of that report intact: grow the live machine to three seats,
// leave Base Camp, then CONTINUE the company and require all three seats to
// still be present.
//
// The player count is a live machine/session choice, not company state.
// CONTINUE still reloads the most-recent company, so that load must preserve
// the live choice and rebuild all three local lobby seats. The company file
// keeps its canonical one-player compatibility byte for old GTL readers.
struct ContinuePlayerCountState {
    bool started = false;
    bool finished = false;
    bool saw_initial_base_camp = false;
    bool saw_seat_add = false;
    bool saw_continued_base_camp = false;
    bool saw_three_seats = false;
    unsigned char live_count_after_continue = 0;
    std::array<bool, 4> visible_seat_cards{};
    bool fourth_slot_offers_a_seat = false;
    std::string founded_slot;
    std::string continued_slot;
};

static int continue_player_count_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<ContinuePlayerCountState*>(data);
    state->started = true;

    if (!wait_for_interactable("begin_new_game", 5000))
        return 0;
    SDL_Delay(750);
    fprintf(stderr, "  [test] founding company for player-count Continue flow\n");
    interact("begin_new_game");

    if (!accept_generated_company_name())
        return 0;

    // picker_prepare_new_game_setup no longer blocks on the campaign intro
    // (issue #186); the campaign select and intro run un-driven under
    // TESTING and the flow lands on Base Camp on its own.
    if (!wait_for_team_menu())
        return 0;
    state->saw_initial_base_camp = true;
    state->founded_slot = og::data::active_company_slot();
    SDL_Delay(750);
    // A slot with no seat in it IS the add door: with one seat claimed, slot
    // one wears ADD PLAYER, and the click lands when it stops wearing it.
    if (!wait_for_interactable_label("seat_card_1", "ADD PLAYER", 5000))
        return 0;
    state->saw_seat_add = true;
    SDL_Delay(300);
    fprintf(stderr, "  [test] adding local seat 2 in Base Camp\n");
    interact("seat_card_1");
    if (!wait_for_interactable_label_change("seat_card_1", "ADD PLAYER", 5000))
        return 0;
    // The add door deliberately rejects a second activation inside 250 ms so
    // a touch release cannot create two seats. Cross that boundary before the
    // intentional second click.
    SDL_Delay(300);
    fprintf(stderr, "  [test] adding local seat 3 in Base Camp\n");
    interact("seat_card_2");
    if (!wait_for_interactable_label_change("seat_card_2", "ADD PLAYER", 5000))
        return 0;

    // Let the release edge clear before injecting the next press.
    SDL_Delay(300);
    fprintf(stderr, "  [test] backing out of the new company's Base Camp\n");
    interact("back");

    if (!wait_for_interactable("continue_game", 10000))
        return 0;
    SDL_Delay(750);
    fprintf(stderr, "  [test] continuing the founded company\n");
    interact("continue_game");

    if (!wait_for_team_menu())
        return 0;
    state->saw_continued_base_camp = true;
    state->continued_slot = og::data::active_company_slot();
    // All four slots are on screen; three of them hold this machine's seats
    // and the fourth still offers one. "Seat card" now means "a slot that is
    // not the ADD PLAYER door".
    const auto seat_card = [](int index) {
        const std::string label =
            interactable_label("seat_card_" + std::to_string(index));
        return !label.empty() && label != "ADD PLAYER" &&
            label != "LOBBY FULL";
    };
    const auto wait_for_three_seat_cards = [&] {
        int elapsed = 0;
        constexpr int poll_interval = 50;
        while (elapsed < 5000) {
            if (seat_card(0) && seat_card(1) && seat_card(2) && !seat_card(3))
                return true;
            SDL_Delay(poll_interval);
            elapsed += poll_interval;
        }
        return false;
    };
    state->saw_three_seats = wait_for_three_seat_cards();
    for (std::size_t index = 0; index < state->visible_seat_cards.size();
         ++index) {
        state->visible_seat_cards[index] = seat_card(static_cast<int>(index));
    }
    state->fourth_slot_offers_a_seat =
        interactable_label("seat_card_3") == "ADD PLAYER";

    // Let CONTINUE's click-release edge clear before clicking BACK; otherwise
    // the synthetic second click can be swallowed and leave picker_main open.
    SDL_Delay(300);
    fprintf(stderr, "  [test] backing out of the continued Base Camp\n");
    interact("back");
    state->finished = true;
    return 0;
}

TEST(NewGame, player_count_survives_back_then_continue)
{
#if defined(DISABLE_MULTIPLAYER) || defined(USE_TOUCH_INPUT)
    GTEST_SKIP()
        << "this build supports one local seat, so the three-seat Continue "
           "regression does not apply";
#endif
    struct ClockReset {
        ~ClockReset()
        {
            og::data::set_company_clock_for_tests(std::nullopt);
        }
    } clock_reset;
    struct FoundedCompanyCleanup {
        std::string slot;
        ~FoundedCompanyCleanup()
        {
            if (slot.empty() || slot == "save0")
                return;
            (void)og::data::set_active_company_slot("save0");
            (void)og::data::delete_company(slot);
        }
    } company_cleanup;

    trace_clear();
    // Outrank any save0 or opt-in stray company created earlier in this
    // process, including when the suite runs shuffled within the same second.
    og::data::set_company_clock_for_tests(4102444800LL); // 2100-01-01 UTC

    ContinuePlayerCountState state;
    SDL_Thread* thread = SDL_CreateThread(
        continue_player_count_injector, "continue_player_count", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    // First main menu founds the company and Base Camp grows the machine's
    // seat declaration. The second hosts CONTINUE; after the second Base Camp
    // BACK, the test gate quits.
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);

    // The injector synchronizes through the locked interactable registry and
    // never reads the SaveData/lobby model. Inspect that model here, after the
    // UI and injector have both stopped, so the regression remains race-free.
    state.live_count_after_continue =
        og::runtime::current_session->myscreen_->save_data.numplayers;
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    company_cleanup.slot = state.founded_slot;

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished) << "the complete user flow should unwind";
    ASSERT_TRUE(state.saw_initial_base_camp);
    ASSERT_TRUE(state.saw_seat_add);
    ASSERT_TRUE(state.saw_continued_base_camp);
    EXPECT_EQ(state.founded_slot, state.continued_slot)
        << "Continue should reopen the company just founded";
    EXPECT_EQ(3, static_cast<int>(state.live_count_after_continue))
        << "loading the company must preserve the machine's live seat count";
    EXPECT_TRUE(state.saw_three_seats)
        << "continued Base Camp must expose exactly the three selected seats";
    EXPECT_TRUE(state.visible_seat_cards[0]);
    EXPECT_TRUE(state.visible_seat_cards[1]);
    EXPECT_TRUE(state.visible_seat_cards[2]);
    EXPECT_FALSE(state.visible_seat_cards[3])
        << "three local players must render exactly three seat cards";
    EXPECT_TRUE(state.fourth_slot_offers_a_seat)
        << "the fourth slot stays on screen as the ADD PLAYER door";

    // The session setting must not leak into the company file. Offset 132 is
    // retained solely so historical GTL readers see a valid one-player save.
    auto company_file = og::io::og_open_read(
        "save/", (state.founded_slot + ".gtl").c_str());
    ASSERT_NE(nullptr, company_file);
    ASSERT_EQ(132, company_file->seek(132, 0));
    std::uint8_t legacy_player_count = 0;
    ASSERT_TRUE(og::io::og_read_exact(
        *company_file, &legacy_player_count, 1, 1));
    EXPECT_EQ(1, static_cast<int>(legacy_player_count))
        << "GTL must retain only its canonical compatibility marker";
}
