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
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/gameplay/guy.h>
#include <optional>
#include <string>
#include <vector>
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

// Removes scratch companies (and their backups) a test wrote into the shared
// user dir. A company file left behind is not inert: CONTINUE opens the
// MOST-RECENT company, so a stray slot with a fresher last_played silently
// redirects the next flow's whole session. Same shape as
// tests/integration/test_cloud_ui.cpp's guard.
struct CompanySlotCleanup {
    std::vector<std::string> slots;
    ~CompanySlotCleanup()
    {
        for (const std::string& slot : slots) {
            for (const og::data::CompanyBackupInfo& backup :
                 og::data::list_company_backups(slot))
                (void)og::data::delete_company_backup(slot, backup.seq);
            (void)remove_user_file("save/" + slot + ".gtl");
        }
    }
};

// Restores whatever fixed clock (if any) the suite had installed.
struct CompanyClockRestore {
    ~CompanyClockRestore() { og::data::set_company_clock_for_tests(std::nullopt); }
};

// Write the in-memory save to `slot` the way the GAME writes a company: through
// the autosave choke point, which stamps last_played_unix_s. A bare
// SaveData::save() leaves the stamp at zero, which is what made these flows
// depend on nothing else in the binary having a fresher company — the CONTINUE
// door opens the most recent one, not the one the test happened to write.
static bool seed_open_company(SaveData& save, const std::string& slot,
                              std::int64_t stamp_s)
{
    og::data::set_company_clock_for_tests(stamp_s);
    const bool ok = og::data::set_active_company_slot(slot) &&
                    og::data::company_autosave(
                        save, og::data::CompanyAutosaveKind::BaseCampMutation) ==
                        SaveDataIoError::None;
    og::data::set_company_clock_for_tests(std::nullopt);
    return ok;
}

// Test: Navigate to hire troops, browse characters with NEXT/PREV, then exit.
//
// Note: We can't click HIRE ME because add_guy() calls prompt_for_string()
// to name the character, which blocks on text input. Instead we test that
// the hire menu loads, character cycling works, and we can exit cleanly.
//
// Flow: Main Menu -> Begin New Game -> (accept company name) ->
//       Team Build -> Hire Troops -> NEXT -> NEXT -> PREV -> Back -> Back

struct HireState {
    bool started = false;
    bool finished = false;
    bool saw_hire_menu = false;
    bool left_hire_menu = false;
    int cycles_completed = 0;
};

// The hire screen's live candidate index, read on the menu thread. PREV/NEXT/
// HIRE ME/BACK all carry fixed text, so there is no per-candidate LABEL to
// watch: the index cycle_guy() writes is the only honest proof that a click
// was consumed. (Counting the clicks the injector SENT is not — an engine
// that dropped every one of them satisfies that count exactly.)
static bool hire_candidate_index(int& out)
{
    return run_on_main_thread([&out] {
        out = static_cast<int>(og::runtime::current_session->current_type_);
    });
}

// Establish the next frame's pointer baseline on the menu thread. A press
// sent while the previous click's release is still queued reads to the menu
// loop as one held pointer, and the new press is simply dropped — which is
// what the flat SDL_Delay between consecutive clicks was buying, expensively
// and without ever proving it worked. Same shape as test_difficulty.cpp's
// interact_times.
static void settle_pointer_between_clicks()
{
    (void)run_on_main_thread([] { reset_mouse_click_tracking(); });
    wait_for_menu_frames(1);
}

// Click a hire cycler and report whether the candidate ACTUALLY moved,
// re-clicking on the documented 300 ms spacing until a 5 s deadline. Same
// shape as test_options_menu.cpp's click_cycle_step.
static bool cycle_hire_candidate(const char* id)
{
    int before = -1;
    if (!hire_candidate_index(before))
        return false;
    if (!interact(id))
        return false;
    const Uint64 deadline = SDL_GetTicks() + 5000;
    Uint64 last_click = SDL_GetTicks();
    for (;;) {
        int now = before;
        if (!hire_candidate_index(now))
            return false;
        if (now != before)
            return true;
        if (SDL_GetTicks() >= deadline) {
            fprintf(stderr,
                    "  [test] '%s' did not advance the hire candidate "
                    "(index stuck at %d)\n", id, before);
            return false;
        }
        if (SDL_GetTicks() - last_click >= 300) {  // re-click spacing only
            if (!interact(id))
                return false;
            last_click = SDL_GetTicks();
        }
        SDL_Delay(20);
    }
}

static int hire_injector(void* data)
{
    og::runtime::ensure_thread_session();
    HireState* state = static_cast<HireState*>(data);
    state->started = true;

    // Wait for main menu
    wait_for_interactable("begin_new_game", 5000);
    wait_for_menu_frames(2);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // §2.2: accept the generated company name at the name-entry screen.
    accept_generated_company_name();

    // No campaign intro here anymore (issue #186: it moved behind the
    // campaign select, skipped under TESTING) — an Escape here would BACK
    // out of the team-build screen instead.

    // New games now land on team build first, then enter hire explicitly.
    if (!wait_for_interactable("hire_troops", 10000)) {
        state->finished = true;
        return 0;
    }
    wait_for_menu_frames(2);
    fprintf(stderr, "  [test] clicking hire_troops\n");
    interact("hire_troops");

    if (wait_for_interactable("hire_me", 10000)) {
        state->saw_hire_menu = true;
        wait_for_menu_frames(2);

        // Cycle through characters: NEXT, NEXT, PREV — counting only the
        // cycles the SCREEN confirmed.
        fprintf(stderr, "  [test] clicking next\n");
        if (cycle_hire_candidate("next"))
            state->cycles_completed++;

        fprintf(stderr, "  [test] clicking next again\n");
        if (cycle_hire_candidate("next"))
            state->cycles_completed++;

        // And back with PREV
        fprintf(stderr, "  [test] clicking prev\n");
        if (cycle_hire_candidate("prev"))
            state->cycles_completed++;
        settle_pointer_between_clicks();
    }

    // Go back to team menu. Never click blind: a BACK fired at a screen that
    // has not settled prints a warning nobody reads, and the flow then fails
    // ten seconds later on an unrelated wait. Waiting for it names the miss
    // where it happens.
    //
    // RESIDUAL, stated plainly: if BACK really is gone, this blocking screen
    // still cannot be unwound from an injector — the engine's Escape hotkey
    // reads SDL's keyboard-state array, which SDL_PushEvent cannot write, so
    // there is no injectable key that substitutes for the button. The flow
    // below then reports left_hire_menu == false once picker_main returns,
    // and if it cannot return, ctest's group timeout is still the backstop.
    fprintf(stderr, "  [test] clicking back from hire menu\n");
    if (wait_for_interactable("back", 5000))
        state->left_hire_menu = interact("back");

    // Back to main menu
    if (!wait_for_interactable("hire_troops", 10000)) {
        state->finished = true;
        return 0;
    }
    wait_for_menu_frames(2);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    if (wait_for_interactable("back", 5000))
        interact("back");

    state->finished = true;
    return 0;
}

TEST(HireTeam, hire_menu_browsing) {
    trace_clear();

    // This flow FOUNDS a company (BEGIN NEW GAME), which repoints the process
    // -wide active slot. Restore it on the way out so the next test starts
    // where it expects to.
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";

    // Start with empty team
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    HireState state;
    SDL_Thread* thread = SDL_CreateThread(hire_injector, "hire_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_hire_menu) << "should have seen the hire menu";
    ASSERT_TRUE(state.left_hire_menu)
        << "BACK must have been on screen and clickable when the flow left "
           "the hire menu";
    // EXACT, and counted from the screen's own candidate index: the old
    // `>= 3` counted the clicks the injector SENT (incremented
    // unconditionally), so an engine that dropped every one of them passed.
    ASSERT_EQ(3, state.cycles_completed)
        << "NEXT/PREV did not advance the hire candidate: only "
        << state.cycles_completed << " of 3 cycles were confirmed";
}

// §2.9 flow 7 + §3.8: HIRE re-enters from the base-camp command strip, a
// successful hire lands as a roster row that is DEPLOYED BY DEFAULT, and the
// hire mutation autosaves the company (the new member is on disk without any
// manual save). Under TESTING the hire name prompt accepts the generated
// name without blocking.
//
// Flow: Main Menu -> Continue -> base camp -> HIRE -> hire_me -> Back ->
//       base camp shows the new row -> Back

struct HireDeployState {
    bool started;
    bool finished;
    bool saw_hire_menu;
    bool hired;
    bool saw_new_row;
};

static int hire_deployed_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* state = static_cast<HireDeployState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    wait_for_menu_frames(2);
    interact("continue_game");

    if (!wait_for_interactable("hire_troops", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    wait_for_menu_frames(2);
    interact("hire_troops");

    if (wait_for_interactable("hire_me", 10000)) {
        state->saw_hire_menu = true;
        wait_for_menu_frames(2);
        fprintf(stderr, "  [test] clicking hire_me\n");
        state->hired = interact("hire_me");
        settle_pointer_between_clicks();
    }

    fprintf(stderr, "  [test] clicking back from hire menu\n");
    if (wait_for_interactable("back", 5000))
        interact("back");

    // Re-entry: the base camp shows the hired member as roster row 1.
    if (wait_for_interactable("roster_dep_1", 10000)) {
        state->saw_new_row = true;
    }
    settle_pointer_between_clicks();
    fprintf(stderr, "  [test] clicking back from base camp\n");
    if (wait_for_interactable("back", 5000))
        interact("back");

    state->finished = true;
    return 0;
}

TEST(HireTeam, hire_from_base_camp_lands_deployed_and_autosaves) {
    trace_clear();

    CompanyClockRestore clock_restore;
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";

    // One existing member + gold to hire with; CONTINUE needs the file on
    // disk (it also serves as the pre-hire disk baseline).
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.scen_num = 1;
    {
        auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
        soldier->name = "VETERAN";
        og::runtime::current_session->myscreen_->save_data.team_list[0] = std::move(soldier);
        og::runtime::current_session->myscreen_->save_data.team_size = 1;
        og::runtime::current_session->myscreen_->save_data.m_totalcash[0] = 100000;
        og::runtime::current_session->myscreen_->save_data.totalcash = 100000;
    }
    // CONTINUE opens the most recent company, so seed this one THROUGH the
    // autosave choke point: a bare SaveData::save() leaves last_played at
    // zero and the flow then belongs to whichever company some other test
    // stamped last.
    ASSERT_TRUE(seed_open_company(og::runtime::current_session->myscreen_->save_data,
                                  "save0", og::data::company_clock_now_s()))
        << "the company under test must be written as the most recent one";

    HireDeployState state = { false, false, false, false, false };
    SDL_Thread* thread = SDL_CreateThread(hire_deployed_injector, "hire_deploy_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.saw_hire_menu) << "HIRE strip button should open the hire screen";
    ASSERT_TRUE(state.hired) << "hire_me should be clickable";
    ASSERT_TRUE(state.saw_new_row)
        << "the hired member must appear as a base-camp roster row on re-entry";

    // In memory: the hire landed and defaults to deployed (§2.5).
    ASSERT_EQ(2, static_cast<int>(og::runtime::current_session->myscreen_->save_data.team_size));
    ASSERT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[1] != nullptr);
    EXPECT_TRUE(og::runtime::current_session->myscreen_->save_data.team_list[1]->deployed)
        << "new hires default to deployed=true";

    // §3.8: the hire mutation AUTOSAVED — the pre-hire disk baseline had one
    // member; the file must now hold the hired member, deployed.
    ASSERT_EQ("save0", og::data::active_company_slot())
        << "the hire flow must still be on the company this test seeded";
    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    ASSERT_EQ(2, static_cast<int>(reloaded.team_size));
    ASSERT_TRUE(reloaded.team_list[1] != nullptr);
    EXPECT_TRUE(reloaded.team_list[1]->deployed)
        << "the hired member must persist deployed via the mutation autosave";
    EXPECT_EQ("VETERAN", reloaded.team_list[0]->name) << "slot 0 untouched";
}

// §3.8, order-free: the hire autosave lands in the company the flow actually
// has OPEN. CONTINUE opens the MOST-RECENT company (§2.1), so a stray company
// file with a fresher last_played — which any test in this binary can leave
// behind, and which `--gtest_shuffle` duly arranges — silently redirects the
// whole flow: the hire is written to a slot nobody asserts on while the test's
// own save0 keeps its pre-hire baseline, and the failure surfaces sixty lines
// later as "2 vs 1" on a reload.
//
// This reproduces that without depending on a shuffle seed, and it fails at
// the point of divergence (which company is open) instead of at the symptom.
TEST(HireTeam, hire_autosaves_into_the_open_company_not_a_stray_slot)
{
    trace_clear();

    // Both companies are scratch slots: save0 is left exactly as the rest of
    // the binary expects to find it (this test must not become the next
    // stray itself).
    CompanySlotCleanup cleanup{{"straycompany", "hireopen"}};
    CompanyClockRestore clock_restore;
    og::data::ScopedActiveCompany pin("hireopen");
    ASSERT_TRUE(pin.applied()) << "hireopen must be a valid company slot";

    const std::int64_t now_s = og::data::company_clock_now_s();

    // The stray: a real, loadable company stamped in the future — the shape a
    // sibling test leaves behind when its scratch slot gets autosaved.
    {
        SaveData stray;
        stray.reset();
        stray.numplayers = 1;
        stray.current_campaign = "gladiator";
        stray.scen_num = 1;
        auto decoy = std::make_unique<guy>(FAMILY_SOLDIER);
        decoy->name = "STRAY";
        stray.team_list[0] = std::move(decoy);
        stray.team_size = 1;
        stray.m_totalcash[0] = 100000;
        stray.totalcash = 100000;
        ASSERT_TRUE(seed_open_company(stray, "straycompany", now_s + 1000000))
            << "the stray company fixture must be written";
    }

    SaveData& save_data = og::runtime::current_session->myscreen_->save_data;
    save_data.reset();
    save_data.numplayers = 1;
    save_data.current_campaign = "gladiator";
    save_data.scen_num = 1;
    {
        auto soldier = std::make_unique<guy>(FAMILY_SOLDIER);
        soldier->name = "VETERAN";
        save_data.team_list[0] = std::move(soldier);
        save_data.team_size = 1;
        save_data.m_totalcash[0] = 100000;
        save_data.totalcash = 100000;
    }
    ASSERT_TRUE(seed_open_company(save_data, "hireopen", now_s + 2000000))
        << "the company under test must be written as the most recent one";

    HireDeployState state = { false, false, false, false, false };
    SDL_Thread* thread =
        SDL_CreateThread(hire_deployed_injector, "hire_stray_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_EQ("hireopen", og::data::active_company_slot())
        << "the hire flow must still be on the company this test seeded — a "
           "stray company file must never take the session over";

    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("hireopen"));
    ASSERT_EQ(2, static_cast<int>(reloaded.team_size))
        << "the hire must have autosaved into the open company";
}
