// #490: BEGIN NEW GAME founds the company on the default campaign and goes
// from name entry to Base Camp with no campaign list on the way; the list is
// SCENARIO > SET CAMPAIGN's alone, and that door still opens it.
//
// The flow starts from a company on another campaign (westlands, mounted), so
// "the default campaign" is proved by the founding and not inherited. The
// campaign browser is not run_menu_screen-hosted and its buttons are not in
// allbuttons, so it is watched through its own entry counter
// (tests/test_campaign_picker_drive.h): the count may not move between the
// main menu and Base Camp, and SET CAMPAIGN must move it by exactly one. The
// un-driven browser accepts the current campaign one presented frame in
// (campaign_picker_testing_set_auto_accept, re-armed for every test), and the
// un-driven intro dismisses itself after its first frame, so neither needs a
// click.
//
// Tier 2 of scripts/check_injector_settles.sh from birth: every wait is an
// edge or a ladder, and every give-up leaves through the shared escape tail.
#include <gtest/gtest.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <string>

#include <openglad/core/test_trace.h>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/session_state.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include "test_camp_save_fixture.h"
#include "test_campaign_picker_drive.h"
#include "test_click_ladder.h"
#include "test_escape_tail.h"
#include "test_interact.h"
#include "test_save_state_guard.h"

void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

namespace
{

PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++)
    {
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

// Every screen the flow can be left on, in precedence order: SCENARIO (which
// also shows Base Camp's strip), Base Camp, name entry, then the main menu.
// The main menu runs under a cap of one, so its second visit answers QUIT
// without drawing: Base Camp's BACK is the way out on the happy path.
constexpr EscapeDoor kNewGameCampaignDoors[] = {
    {"set_campaign", "back"},
    {"go", "back"},
    {"company_name_accept", "back"},
    {"quit", "quit"},
};

struct NewGameCampaignState
{
    // Published by the test body the instant picker_main returns.
    std::atomic<bool> main_left{false};
    std::uint64_t entered_before_new_game = 0;
    std::uint64_t entered_at_base_camp = 0;
    std::uint64_t entered_after_set_campaign = 0;
    bool saw_base_camp = false;
    std::string campaign_at_base_camp;
    short scen_at_base_camp = -1;
    std::string mounted_at_base_camp;
};

// Base Camp is up: its GO and its SCENARIO strip door are published.
bool wait_for_base_camp(int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 5)
    {
        if (has_interactable("go") && has_interactable("scenario"))
            return true;
        SDL_Delay(5);
    }
    return false;
}

// The campaign browser has been entered since `baseline`.
bool wait_for_browser_entry(std::uint64_t baseline, int timeout_ms)
{
    for (int waited = 0; waited < timeout_ms; waited += 5)
    {
        if (campaign_picker_testing_entered_count() > baseline)
            return true;
        SDL_Delay(5);
    }
    return false;
}

int new_game_campaign_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NewGameCampaignState*>(data);

    // A browser entry this flow did not ask for is closed by its own abort
    // door: while it owns the main thread its buttons are its own array, and
    // a click aimed at an allbuttons id would land inside it.
    std::uint64_t seen_entries = campaign_picker_testing_entered_count();
    const auto escape = [state, &seen_entries](int leg, const char* why) {
        return escape_to_the_main_thread(
            state->main_left, leg, why, kNewGameCampaignDoors,
            [&seen_entries] {
                const std::uint64_t entered =
                    campaign_picker_testing_entered_count();
                if (entered > seen_entries)
                {
                    campaign_picker_testing_abort();
                    seen_entries = entered;
                    return true;
                }
                return false;
            });
    };

    // -- Leg 1: the main menu has composed --
    if (!wait_for_interactable("begin_new_game", 10000) ||
        !wait_for_menu_frames(2))
        return escape(1, "the main menu never published BEGIN NEW GAME");
    state->entered_before_new_game = campaign_picker_testing_entered_count();

    // -- Leg 2: BEGIN NEW GAME opens name entry --
    if (!click_until_edge("begin_new_game", [](int ms) {
            return wait_for_interactable("company_name_accept", ms);
        }))
        return escape(2, "BEGIN NEW GAME never opened name entry");
    if (!wait_for_menu_frames(2))
        return escape(2, "name entry never composed a frame");

    // -- Leg 3: ACCEPT founds the company and leaves name entry --
    if (!click_until_edge(
            "company_name_accept",
            [](int ms) {
                for (int waited = 0; waited < ms; waited += 5)
                {
                    if (!has_interactable("company_name_accept"))
                        return true;
                    SDL_Delay(5);
                }
                return false;
            },
            "accept", 3, 2500, "name_entry"))
        return escape(3, "ACCEPT never left name entry");

    // -- Leg 4: Base Camp, with no campaign list on the way --
    if (!wait_for_base_camp(20000) || !wait_for_menu_frames(2))
        return escape(4, "the founded company never reached Base Camp");
    state->saw_base_camp = true;
    state->entered_at_base_camp = campaign_picker_testing_entered_count();
    seen_entries = std::max(seen_entries, state->entered_at_base_camp);
    if (!run_on_main_thread([state] {
            const SaveData& save =
                og::runtime::current_session->myscreen_->save_data;
            state->campaign_at_base_camp = save.current_campaign;
            state->scen_at_base_camp = save.scen_num;
            state->mounted_at_base_camp = get_mounted_campaign();
        }))
        return escape(4, "Base Camp never ran the save read");

    // -- Leg 5: SCENARIO opens its subscreen --
    if (!click_until_edge("scenario", [](int ms) {
            return wait_for_interactable("set_campaign", ms);
        }))
        return escape(5, "SCENARIO never opened its subscreen");
    if (!wait_for_menu_frames(2))
        return escape(5, "the SCENARIO subscreen never composed a frame");

    // -- Leg 6: SET CAMPAIGN opens the campaign browser --
    const std::uint64_t before_set_campaign =
        campaign_picker_testing_entered_count();
    if (!click_until_edge("set_campaign", [before_set_campaign](int ms) {
            return wait_for_browser_entry(before_set_campaign, ms);
        }))
        return escape(6, "SET CAMPAIGN never opened the campaign browser");

    // -- Leg 7: the browser accepted and SCENARIO composes again --
    // Read the count only after SCENARIO completed frames with the click
    // released, so a press still held when the browser closed would show as
    // a second entry instead of hiding in the read.
    if (!wait_for_interactable("set_campaign", 10000) ||
        !wait_for_menu_frames(2))
        return escape(7, "the SCENARIO subscreen never came back");
    state->entered_after_set_campaign =
        campaign_picker_testing_entered_count();
    seen_entries = std::max(seen_entries, state->entered_after_set_campaign);

    // SCENARIO's BACK, Base Camp's BACK and the capped main menu end the
    // flow through the tail: after picker_main returns no pump is left.
    return escape(0, "");
}

} // namespace

TEST(NewGameCampaign,
     a_new_game_skips_the_campaign_list_and_set_campaign_still_opens_it)
{
    og::test::ScopedCampaignMountState mount_restore;
    trace_clear();
    // A company on another campaign, mounted: the founding must move both
    // the save and the mount to the default campaign by itself.
    write_save0_with_soldiers("westlands", 3, 2);
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("westlands"));

    NewGameCampaignState state;
    SDL_Thread* thread = SDL_CreateThread(new_game_campaign_injector,
                                          "new_game_campaign", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.main_left.store(true);

    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result
        << " (1=main menu, 2=name entry, 3=ACCEPT, 4=Base Camp, 5=SCENARIO, "
           "6=SET CAMPAIGN opened the browser, 7=SCENARIO back)";
    ASSERT_TRUE(state.saw_base_camp);
    EXPECT_EQ(state.entered_before_new_game, state.entered_at_base_camp)
        << "#490: a new game must not open the campaign list";
    EXPECT_EQ("gladiator", state.campaign_at_base_camp)
        << "the company is founded on the default campaign";
    EXPECT_EQ(1, state.scen_at_base_camp);
    EXPECT_EQ("gladiator", state.mounted_at_base_camp)
        << "the founding mounts the default campaign";
    EXPECT_TRUE(trace_contains("help", "campaign intro gladiator"))
        << "the founded campaign's intro plays between name entry and Base "
           "Camp";
    EXPECT_EQ(state.entered_at_base_camp + 1,
              state.entered_after_set_campaign)
        << "SCENARIO > SET CAMPAIGN opens the campaign list, exactly once";
    EXPECT_TRUE(trace_contains("campaign_picker", "auto-accept gladiator"))
        << "the browser SET CAMPAIGN opened accepted the current campaign";
}
