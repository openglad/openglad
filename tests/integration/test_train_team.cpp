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
#include "test_company_cleanup.h"
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
    // The stat-button handshake: what the WORKING copy held before the
    // click, what it held after it was consumed, and the price the screen
    // then quoted. 0/-1 mean "the click was never proven consumed".
    short original_strength = -1;
    short original_dexterity = -1;
    short strength_after_click = -1;
    short dexterity_after_click = -1;
    std::uint32_t cost_after_clicks = 0;
    bool charged_before_accept = false;
    // DETAILS round trips (open + BACK) and NEXT presses that actually moved
    // the session to another roster slot. Counted, never assumed: a press
    // that the screen never consumed leaves these short.
    int details_round_trips = 0;
    int cycles = 0;
};

// Read the live TrainSession on the MENU thread — the injector must never
// touch picker state from its own thread.
static bool main_thread_train_stats(short& strength, short& dexterity,
                                    std::uint32_t& cost)
{
    bool ok = false;
    const bool ran = run_on_main_thread([&]() {
        og::ui::TrainSession* session = pks().train_session;
        if (!session || session->empty())
            return;
        strength = session->working_copy().strength;
        dexterity = session->working_copy().dexterity;
        cost = session->current_cost();
        ok = true;
    });
    return ran && ok;
}

// Baseline the pointer on the MENU thread before a press into an
// engine-hosted screen. Both click paths detect an up->down EDGE, so a press
// sent while the previous one's release is still standing is simply dropped;
// the engine clears that flag when it changes screens, not when a legacy
// submenu hands control back. Same shape as test_hire_team.cpp's
// settle_pointer_between_clicks.
static void settle_pointer_between_clicks()
{
    (void)run_on_main_thread([] { reset_mouse_click_tracking(); });
    wait_for_menu_frames(1);
}

// The roster slot the session is seated on, read on the MENU thread. This is
// what NEXT (ButtonAction::CycleTeamGuy -> TrainSession::next_member) moves,
// so it is the proof a NEXT press was consumed.
static bool main_thread_train_slot(int& slot)
{
    bool ok = false;
    const bool ran = run_on_main_thread([&]() {
        og::ui::TrainSession* session = pks().train_session;
        if (!session || session->empty())
            return;
        slot = session->current_slot();
        ok = true;
    });
    return ran && ok;
}

// The DETAILS submenu is NOT engine-hosted: create_detail_menu (picker.cpp)
// runs its own legacy loop, so while it is up run_menu_screen completes no
// frame (wait_for_menu_frames can never settle) and pumps no main-thread task
// (run_on_main_thread would burn its whole cancellation ceiling). Its own
// oracle is the published button set: DETAILS swaps the twenty train rows for
// BACK/PROMOTE, so "promote exists" is exactly "the details screen is up" --
// read through get_button_ids(), which reports hidden rows too (PROMOTE is
// hidden for a family with no promotion).
static bool details_screen_is_open()
{
    for (const std::string& id : get_button_ids())
        if (id == "promote")
            return true;
    return false;
}

static bool wait_for_details_screen(bool open, int timeout_ms)
{
    const Uint64 deadline = SDL_GetTicks() + static_cast<Uint64>(timeout_ms);
    for (;;) {
        if (details_screen_is_open() == open)
            return true;
        if (SDL_GetTicks() >= deadline) {
            fprintf(stderr, "  [test] TIMEOUT waiting for the DETAILS screen "
                            "to %s (%d ms)\n", open ? "open" : "close",
                    timeout_ms);
            return false;
        }
        SDL_Delay(10);
    }
}

// A press across the DETAILS boundary needs its own pointer baseline. Both
// loops detect a click as the up->down EDGE (picker_input.cpp, leftmouse:
// `mymouse.left && !input_hw.picker_was_left_down`), and the legacy loop
// never calls reset_mouse_click_tracking the way the engine does between
// screens. So the press that opened DETAILS leaves picker_was_left_down TRUE
// behind it, the next press shows no edge, and it is swallowed whole -- which
// is what the flat SDL_Delay(300) settles here used to hide: the DETAILS and
// BACK presses were mostly no-ops and nothing checked. Pushing a release
// first, and letting the loop poll it, is the one way an outside thread can
// clear that flag when no main-thread pump is running.
static bool press_across_details_boundary(const char* button_id)
{
    int win_x = -1;
    int win_y = -1;
    if (!interact_window_point(button_id, win_x, win_y))
        return false;
    inject_mouse_up(win_x, win_y);   // clear the stale "pointer still down"
    SDL_Delay(20);                   // one poll of the loop, not a settle
    inject_click(win_x, win_y, 100);
    return true;
}

// Press, then wait the WHOLE budget for the screen to change; re-press only
// after the budget expired with nothing having happened at all. Deliberately
// NOT the 300 ms re-click cadence the stat rows use: BACK exists on both
// screens, so a second BACK sent just as the first one landed would walk out
// of the train menu as well.
static bool click_until_details_screen(const char* button_id, bool open)
{
    for (int attempt = 0; attempt < 2; ++attempt) {
        if (!press_across_details_boundary(button_id))
            return false;
        if (wait_for_details_screen(open, 10000))
            return true;
        fprintf(stderr, "  [test] '%s' left the screen unchanged; re-pressing\n",
                button_id);
    }
    return false;
}

// One verified NEXT: press, then poll the seated slot (re-clicking on the
// dropped-press cadence, bounded) until the session moves to another member.
static bool train_click_next()
{
    int before = -1;
    if (!main_thread_train_slot(before))
        return false;
    if (!interact("next"))
        return false;
    const Uint64 deadline = SDL_GetTicks() + 5000;
    Uint64 last_click = SDL_GetTicks();
    for (;;) {
        int now = before;
        if (!main_thread_train_slot(now))
            return false;
        if (now != before)
            return true;
        if (SDL_GetTicks() >= deadline) {
            fprintf(stderr, "  [test] NEXT never moved the session off slot "
                            "%d\n", before);
            return false;
        }
        if (SDL_GetTicks() - last_click >= 300) {
            (void)interact("next");
            last_click = SDL_GetTicks();
        }
        SDL_Delay(20);
    }
}

// One verified stat click: press, then poll the working copy (re-clicking on
// the dropped-press pattern, bounded) until it has moved by exactly +1.
// ButtonAction::IncreaseStat -> TrainSession::increase_stat raises the
// working copy by one and quotes a price; nothing is charged until accept.
static bool train_click_stat_step(const char* button_id, bool dexterity,
                                  short& before_out, short& after_out,
                                  std::uint32_t& cost_out)
{
    short str_now = 0, dex_now = 0;
    std::uint32_t cost = 0;
    if (!main_thread_train_stats(str_now, dex_now, cost))
        return false;
    const short before = dexterity ? dex_now : str_now;

    const Uint64 deadline = SDL_GetTicks() + 5000;
    interact(button_id);
    Uint64 last_click = SDL_GetTicks();
    for (;;) {
        if (!main_thread_train_stats(str_now, dex_now, cost))
            return false;
        const short now = dexterity ? dex_now : str_now;
        if (now == before + 1) {
            before_out = before;
            after_out = now;
            cost_out = cost;
            return true;
        }
        if (now != before)
            return false;  // moved by something other than this click
        if (SDL_GetTicks() >= deadline) {
            fprintf(stderr, "  [test] %s never reached %d (stuck at %d)\n",
                    button_id, before + 1, now);
            return false;
        }
        if (SDL_GetTicks() - last_click >= 300) {
            interact(button_id);
            last_click = SDL_GetTicks();
        }
        SDL_Delay(20);
    }
}

static int train_injector(void* data)
{
    og::runtime::ensure_thread_session();
    TrainState* state = static_cast<TrainState*>(data);
    state->started = true;

    // Wait for main menu. Every settle here is a COMPLETED engine frame, not
    // a clock: under TESTING FadeBetween is a single blit, so a flat delay
    // waited for an animation that never runs and proved nothing about the
    // incoming screen (scripts/check_injector_settles.sh).
    wait_for_interactable("continue_game", 5000);
    wait_for_menu_frames(2);

    fprintf(stderr, "  [test] clicking continue_game\n");
    interact("continue_game");

    // Wait for team menu
    wait_for_interactable("roster_row_0", 10000);
    wait_for_menu_frames(2);

    // Click the roster row body (§9.11: the row IS the train affordance)
    fprintf(stderr, "  [test] clicking roster_row_0 (§9.11 row-click train)\n");
    interact("roster_row_0");

    // Wait for train menu buttons
    if (wait_for_interactable("inc_str", 10000)) {
        state->saw_train_menu = true;
        wait_for_menu_frames(2);

        // Strength: the click is proven by the working copy moving +1.
        fprintf(stderr, "  [test] clicking inc_str\n");
        (void)train_click_stat_step("inc_str", /*dexterity=*/false,
                                    state->original_strength,
                                    state->strength_after_click,
                                    state->cost_after_clicks);

        // Dexterity: same handshake on the other stat row.
        fprintf(stderr, "  [test] clicking inc_dex\n");
        (void)train_click_stat_step("inc_dex", /*dexterity=*/true,
                                    state->original_dexterity,
                                    state->dexterity_after_click,
                                    state->cost_after_clicks);

        // Nothing is charged until ACCEPT: the wallet is still whole here.
        (void)run_on_main_thread([state]() {
            state->charged_before_accept =
                og::runtime::current_session->myscreen_->save_data.totalcash
                != 50000u;
        });

        // Open details across several classes to exercise detail rendering
        // branches. Each leg is counted only when the screen actually
        // changed, so a DETAILS door or a NEXT press the menu never consumed
        // shows up as a short count in the test instead of as a green run.
        for (int i = 0; i < 5; i++) {
            if (!wait_for_interactable("details", 10000))
                break;
            fprintf(stderr, "  [test] clicking details (%d)\n", i + 1);
            if (!click_until_details_screen("details", /*open=*/true))
                break;
            fprintf(stderr, "  [test] clicking back from details (%d)\n", i + 1);
            if (!click_until_details_screen("back", /*open=*/false))
                break;
            state->details_round_trips++;
            wait_for_menu_frames(1);

            if (i < 4) {
                fprintf(stderr, "  [test] clicking next (%d)\n", i + 1);
                if (!train_click_next())
                    break;
                state->cycles++;
            }
        }

        // Go back
        settle_pointer_between_clicks();
        fprintf(stderr, "  [test] clicking back from train menu\n");
        interact("back");
    }

    // Back in team menu
    wait_for_interactable("back", 10000);
    wait_for_menu_frames(2);
    settle_pointer_between_clicks();
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

    // CONTINUE opens the MOST RECENT company on disk, not the one this test
    // wrote: a bare SaveData::save() never stamps last_played_unix_s, so a
    // company another test founded would take the session over silently.
    // Seed through the autosave choke point that stamps, and check after the
    // flow which company it actually got.
    ScopedCompanyFileCleanup founded_cleanup;
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";

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

    ASSERT_TRUE(seed_open_company(
        og::runtime::current_session->myscreen_->save_data, "save0",
        newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";

    TrainState state{};
    SDL_Thread* thread = SDL_CreateThread(train_injector, "train_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_EQ("save0", og::data::active_company_slot())
        << "the flow must have run on the company this test seeded";
    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_train_menu) << "should have entered the train menu";

    // Each stat press was consumed by TrainSession::increase_stat: the
    // WORKING copy moved by exactly one, and the screen then quoted a price.
    ASSERT_GE(state.original_strength, 0)
        << "the inc_str press was never consumed by the train session";
    EXPECT_EQ(state.original_strength + 1, state.strength_after_click)
        << "STR + raises the working copy's strength by exactly one";
    ASSERT_GE(state.original_dexterity, 0)
        << "the inc_dex press was never consumed by the train session";
    EXPECT_EQ(state.original_dexterity + 1, state.dexterity_after_click)
        << "DEX + raises the working copy's dexterity by exactly one";
    // The quote is knowable to the coin. This member is a level-10 ARCHMAGE
    // still on its base stats with exp 0, so calculate_train_cost
    // (picker_common.cpp) charges the level's whole XP debt --
    // calculate_exp(10) == 306000, the unrolled recurrence
    // sum(8000 + 2000*(k-1) + 4000*(k-2)) for k=2..10 in src/gameplay/guy.cpp
    // (NOT the stale 115200 in that function's comment table) -- plus the
    // first point of each raised stat, which costs pow(1, exponent) * its
    // rate: STR 30 and DEX 20 from
    // packs/core/families/living-17-archmage.lua.
    EXPECT_EQ(306000u + 30u + 20u, state.cost_after_clicks)
        << "STR +1 and DEX +1 on a base-stat level-10 archmage quote exactly "
           "calculate_exp(10) + 30 + 20";

    // DETAILS opened and closed once per member, and NEXT moved the session
    // to another roster slot every time it was pressed.
    EXPECT_EQ(5, state.details_round_trips)
        << "every DETAILS press must open the detail screen and every BACK "
           "must return to the train menu";
    EXPECT_EQ(4, state.cycles)
        << "each NEXT press must seat the train session on another member";

    // ... and BACK charged nothing: the cost is only taken on accept, and the
    // real roster member keeps its original stats.
    EXPECT_FALSE(state.charged_before_accept)
        << "training must not charge the company before ACCEPT";
    SaveData& after = og::runtime::current_session->myscreen_->save_data;
    EXPECT_EQ(50000u, after.totalcash)
        << "leaving the train screen with BACK must not spend a coin";
    ASSERT_NE(nullptr, after.team_list[0].get())
        << "the trained roster slot must still hold its member";
    EXPECT_EQ(state.original_strength, after.team_list[0]->strength)
        << "an unaccepted raise must never reach the real roster member";
    EXPECT_EQ(state.original_dexterity, after.team_list[0]->dexterity)
        << "an unaccepted raise must never reach the real roster member";
}

// WP7 shuffle seed 9: TrainTeam.train_team ran on a company some other test
// founded, whose roster is EMPTY, so the train menu never appeared and the
// failure surfaced sixty lines away as a missing roster row. This reproduces
// it without a shuffle seed and fails at the point of divergence (which
// company is open) instead of at the symptom.
TEST(TrainTeam, train_team_runs_on_the_open_company_not_a_stray_slot)
{
    trace_clear();

    ScopedCompanyFileCleanup founded_cleanup;
    og::data::ScopedActiveCompany pin("trainopen");
    ASSERT_TRUE(pin.applied()) << "trainopen must be a valid company slot";

    const std::int64_t base_s = newest_company_stamp();

    // The stray: a loadable, EMPTY-roster company stamped ahead of now — the
    // exact shape CampaignAndLevelPicker's BEGIN NEW GAME leaves behind.
    {
        SaveData stray;
        stray.reset();
        stray.numplayers = 1;
        stray.current_campaign = "gladiator";
        stray.scen_num = 1;
        stray.totalcash = 50000;
        ASSERT_TRUE(seed_open_company(stray, "straycompany", base_s + 1000))
            << "the stray company fixture must be written";
    }

    // The company under test: five level-10 members, stamped newer still.
    SaveData& save_data = og::runtime::current_session->myscreen_->save_data;
    save_data.reset();
    save_data.numplayers = 1;
    save_data.current_campaign = "gladiator";
    save_data.scen_num = 1;
    save_data.totalcash = 50000;
    {
        static const int kFamilies[5] = {FAMILY_ARCHMAGE, FAMILY_CLERIC,
                                         FAMILY_DRUID, FAMILY_THIEF,
                                         FAMILY_ORC};
        for (int i = 0; i < 5; ++i) {
            auto member = std::make_unique<guy>(kFamilies[i]);
            member->level = 10;
            save_data.team_list[static_cast<std::size_t>(i)] =
                std::move(member);
        }
        save_data.team_size = 5;
    }
    ASSERT_TRUE(seed_open_company(save_data, "trainopen", base_s + 2000))
        << "the company under test must be written as the most recent one";

    TrainState state{};
    SDL_Thread* thread =
        SDL_CreateThread(train_injector, "train_stray_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_EQ("trainopen", og::data::active_company_slot())
        << "the train flow must still be on the company this test seeded — a "
           "stray company file must never take the session over";
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
}
