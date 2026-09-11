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
    bool started;
    bool finished;
    bool saw_hire_menu;
    int cycles_completed;
};

static int hire_injector(void* data)
{
    og::runtime::ensure_thread_session();
    HireState* state = static_cast<HireState*>(data);
    state->started = true;

    // Wait for main menu
    wait_for_interactable("begin_new_game", 5000);
    SDL_Delay(750);

    fprintf(stderr, "  [test] clicking begin_new_game\n");
    interact("begin_new_game");

    // §2.2: accept the generated company name at the name-entry screen.
    accept_generated_company_name();

    // No campaign intro here anymore (issue #186: it moved behind the
    // campaign select, skipped under TESTING) — an Escape here would BACK
    // out of the team-build screen instead.

    // New games now land on team build first, then enter hire explicitly.
    SDL_Delay(500);
    wait_for_interactable("hire_troops", 10000);
    SDL_Delay(300);
    fprintf(stderr, "  [test] clicking hire_troops\n");
    interact("hire_troops");

    SDL_Delay(500);
    if (wait_for_interactable("hire_me", 10000)) {
        state->saw_hire_menu = true;
        SDL_Delay(500);

        // Cycle through characters with NEXT
        fprintf(stderr, "  [test] clicking next\n");
        interact("next");
        state->cycles_completed++;
        SDL_Delay(300);

        fprintf(stderr, "  [test] clicking next again\n");
        interact("next");
        state->cycles_completed++;
        SDL_Delay(300);

        // And back with PREV
        fprintf(stderr, "  [test] clicking prev\n");
        interact("prev");
        state->cycles_completed++;
        SDL_Delay(300);
    }

    // Go back to team menu
    fprintf(stderr, "  [test] clicking back from hire menu\n");
    interact("back");

    // Back to main menu
    SDL_Delay(500);
    wait_for_interactable("hire_troops", 10000);
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking back from team menu\n");
    interact("back");

    state->finished = true;
    return 0;
}

TEST(HireTeam, hire_menu_browsing) {
    trace_clear();

    // Start with empty team
    og::runtime::current_session->myscreen_->save_data.reset();
    og::runtime::current_session->myscreen_->save_data.numplayers = 1;
    og::runtime::current_session->myscreen_->save_data.current_campaign = "gladiator";
    og::runtime::current_session->myscreen_->save_data.save("save0");

    HireState state = { false, false, false, 0 };
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
    ASSERT_TRUE(state.cycles_completed >= 3) << "should have cycled through characters 3 times";
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
    SDL_Delay(750);
    interact("continue_game");

    SDL_Delay(500);
    if (!wait_for_interactable("hire_troops", 10000)) {
        state->finished = true;
        inject_key_press(SDLK_ESCAPE, 10);
        return 0;
    }
    SDL_Delay(750);
    interact("hire_troops");

    SDL_Delay(500);
    if (wait_for_interactable("hire_me", 10000)) {
        state->saw_hire_menu = true;
        SDL_Delay(500);
        fprintf(stderr, "  [test] clicking hire_me\n");
        interact("hire_me");
        state->hired = true;
        SDL_Delay(500);
    }

    fprintf(stderr, "  [test] clicking back from hire menu\n");
    interact("back");

    // Re-entry: the base camp shows the hired member as roster row 1.
    SDL_Delay(500);
    if (wait_for_interactable("roster_dep_1", 10000)) {
        state->saw_new_row = true;
    }
    SDL_Delay(750);
    fprintf(stderr, "  [test] clicking back from base camp\n");
    interact("back");

    state->finished = true;
    return 0;
}

TEST(HireTeam, hire_from_base_camp_lands_deployed_and_autosaves) {
    trace_clear();

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
    og::runtime::current_session->myscreen_->save_data.save("save0");

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

    CompanySlotCleanup cleanup{{"straycompany"}};
    CompanyClockRestore clock_restore;
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";

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
    ASSERT_TRUE(save_data.save("save0"));

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
    ASSERT_EQ("save0", og::data::active_company_slot())
        << "the hire flow must still be on the company this test seeded — a "
           "stray company file must never take the session over";

    SaveData reloaded;
    ASSERT_TRUE(reloaded.load("save0"));
    ASSERT_EQ(2, static_cast<int>(reloaded.team_size))
        << "the hire must have autosaved into the open company";
}
