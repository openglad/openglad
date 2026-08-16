// SDL MISSIONS subscreen (issue #206): the scripted campaign mission book.
// A synthetic picker (root page + arena sub-page + costed action) is
// registered through the real pack-script registry against the mounted
// gladiator campaign; the injector walks Base Camp -> SCENARIO -> MISSIONS,
// buys the action (debit + state write-through + page refetch), exercises
// the invalid-level rollback, activates a level row (the SDL level-set
// tail), and unwinds. A second test pins the SCENARIO button's visibility
// arm: MISSIONS hidden without a registration, shown with one.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/script/campaign_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/interface/button.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>
#include "../../src/interface/ui/picker_sdl_defs.h"
#include "test_input_helpers.h"
#include "test_interact.h"

#include <SDL3/SDL.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// Picker entry points for the injector-driven flows.
void picker_main(Sint32 argc, char** argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
// Presenter pause handshake (TESTING; the uxshots capture seam).
extern std::atomic_bool g_test_present_pause_requested;
extern std::atomic_bool g_test_present_paused;
// The SCENARIO subscreen's per-frame gating sync (picker_team_build.cpp;
// declared locally by every consumer — repo pattern).
void sync_scenario_menu_host_control_visibility(button* buttons,
                                                int num_buttons,
                                                int& highlighted_button);

namespace {

screen* test_screen()
{
    return og::runtime::current_session->myscreen_;
}

void cleanup_picker_state()
{
    PickerState& state = *og::runtime::current_session->picker_;
    for (int i = 0; i < 5; i++) {
        state.backdrops[static_cast<std::size_t>(i)].reset();
        state.backpics[i].free();
    }
    clear_allbuttons();
    og::runtime::current_session->localbuttons_ = nullptr;
    state.main_columns_pix.reset();
    state.main_columns_data.free();
    state.main_title_logo_pix.reset();
    state.main_title_logo_data.free();
}

// Wait until the (visible) interactable `id` shows label `want`.
bool wait_for_interactable_label(const std::string& id, const std::string& want,
                                 int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == id && !item.hidden && item.label == want)
                return true;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' label '%s'\n",
            id.c_str(), want.c_str());
    return false;
}

// Wait until a (visible) interactable `id` exists at game coords (x, y) —
// disambiguates the per-screen "back" buttons by their geometry.
bool wait_for_interactable_at(const std::string& id, int x, int y,
                              int timeout_ms)
{
    int elapsed = 0;
    while (elapsed < timeout_ms) {
        for (const Interactable& item : get_interactables()) {
            if (item.id == id && !item.hidden && item.x == x && item.y == y)
                return true;
        }
        SDL_Delay(50);
        elapsed += 50;
    }
    fprintf(stderr, "  [interact] TIMEOUT waiting for '%s' at (%d,%d)\n",
            id.c_str(), x, y);
    return false;
}

// Stash/restore the picker save across an injector flow (the test_ctf_ui
// pattern, plus the #206 campaign-state book this flow writes into).
struct SavedPickerSave
{
    std::array<std::unique_ptr<guy>, MAX_TEAM_SIZE> team_list;
    SaveData snapshot_fields;
    std::map<std::string, std::vector<std::pair<std::string, std::int32_t>>>
        campaign_state;

    SavedPickerSave()
    {
        SaveData& save = test_screen()->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            team_list[static_cast<std::size_t>(i)] =
                std::move(save.team_list[static_cast<std::size_t>(i)]);
        snapshot_fields.team_size = save.team_size;
        snapshot_fields.my_team = save.my_team;
        snapshot_fields.numplayers = save.numplayers;
        snapshot_fields.allied_mode = save.allied_mode;
        snapshot_fields.scen_num = save.scen_num;
        snapshot_fields.current_campaign = save.current_campaign;
        for (int t = 0; t < 4; ++t)
            snapshot_fields.m_totalcash[t] = save.m_totalcash[t];
        campaign_state = save.campaign_state;
    }

    ~SavedPickerSave()
    {
        SaveData& save = test_screen()->save_data;
        for (int i = 0; i < MAX_TEAM_SIZE; ++i)
            save.team_list[static_cast<std::size_t>(i)] =
                std::move(team_list[static_cast<std::size_t>(i)]);
        save.team_size = snapshot_fields.team_size;
        save.my_team = snapshot_fields.my_team;
        save.numplayers = snapshot_fields.numplayers;
        save.allied_mode = snapshot_fields.allied_mode;
        save.scen_num = snapshot_fields.scen_num;
        save.current_campaign = snapshot_fields.current_campaign;
        for (int t = 0; t < 4; ++t)
            save.m_totalcash[t] = snapshot_fields.m_totalcash[t];
        save.campaign_state = campaign_state;
    }
};

// Save/restore the pack-script registry around a synthetic registration.
// The gladiator campaign is mounted (same-id remounts are no-ops), so the
// save0 load inside picker_main never re-walks the registry from disk and
// the synthetic chunk survives the whole flow. The chunk name deliberately
// does NOT start with `packs/` (the pack-Lua coverage inventory rule).
class SyntheticCampaignScriptGuard
{
public:
    SyntheticCampaignScriptGuard() : saved_(og::script::pack_scripts()) {}

    static void install(const char* source)
    {
        og::script::register_pack_script(
            {"test.missions", "missionstest/scripts/c.lua", source});
    }

    ~SyntheticCampaignScriptGuard()
    {
        og::script::clear_pack_scripts();
        for (const og::script::PackScript& script : saved_)
            og::script::register_pack_script(script);
    }

private:
    std::vector<og::script::PackScript> saved_;
};

// Optional visual-verification capture (the uxshots PresentedFramePause
// handshake, minimal form): dump the settled 320x200 frame as a PPM when
// UXSHOTS_DIR is set; a no-op otherwise. Runs on the injector thread.
void capture_missions_frame(const char* name)
{
    const char* output_dir = std::getenv("UXSHOTS_DIR");
    if (output_dir == nullptr || output_dir[0] == '\0')
        return;
    std::error_code error;
    std::filesystem::create_directories(output_dir, error);
    if (error)
        return;

    bool expected = false;
    if (!g_test_present_pause_requested.compare_exchange_strong(
            expected, true, std::memory_order_acq_rel))
        return;
    const Uint64 deadline = SDL_GetTicks() + 30000;
    while (!g_test_present_paused.load(std::memory_order_acquire)) {
        if (SDL_GetTicks() >= deadline) {
            g_test_present_pause_requested.store(false,
                                                 std::memory_order_release);
            return;
        }
        SDL_Delay(1);
    }

    std::vector<Uint8> rgb;
    rgb.reserve(320 * 200 * 3);
    screen* scr = test_screen();
    for (int y = 0; y < 200; ++y) {
        for (int x = 0; x < 320; ++x) {
            Uint8 r = 0, g = 0, b = 0;
            scr->get_pixel(x, y, &r, &g, &b);
            rgb.push_back(r);
            rgb.push_back(g);
            rgb.push_back(b);
        }
    }
    g_test_present_pause_requested.store(false, std::memory_order_release);

    const std::string path = std::string(output_dir) + "/" + name + ".ppm";
    FILE* f = fopen(path.c_str(), "wb");
    if (f == nullptr)
        return;
    fprintf(f, "P6\n320 200\n255\n");
    fwrite(rgb.data(), sizeof(Uint8), rgb.size(), f);
    fclose(f);
    fprintf(stderr, "  [uxshot] wrote %s\n", path.c_str());
}

void write_save0_with_two_soldiers(const std::string& campaign, short scen_num)
{
    SaveData& save = test_screen()->save_data;
    for (auto& slot : save.team_list)
        slot.reset();
    save.team_size = 0;
    const char* names[] = {"Alpha", "Beta"};
    for (std::size_t i = 0; i < 2; ++i)
    {
        save.team_list[i] = std::make_unique<guy>(FAMILY_SOLDIER);
        save.team_list[i]->name = names[i];
        save.team_list[i]->teamnum = 0;
    }
    save.team_size = 2;
    save.my_team = 0;
    save.numplayers = 1;
    save.allied_mode = 0;
    save.scen_num = scen_num;
    save.current_campaign = campaign;
    save.current_levels.clear();
    save.current_levels[campaign] = scen_num;
    save.m_totalcash[0] = 5000;
    save.campaign_state.clear();
    ASSERT_TRUE(save.save("save0"));
}

// The synthetic picker: a root page (arena door + costed shop action whose
// label re-derives from the decision book) and the arena page (one bogus
// level for the rollback path, one real gladiator level).
constexpr const char* kMissionsScript = R"LUA(og.register_campaign_hooks({
  vars = { "kit" },
  picker_menu = function(page_id)
    if page_id == "" then
      local kit_label = "FIELD KIT"
      if og.campaign_state_get("kit") == 1 then
        kit_label = "KIT OWNED"
      end
      return {
        title = "MISSION BOOK",
        lines = { "The Gamesmaster opens the book." },
        entries = {
          { id = "arenas", label = "ARENAS", kind = "page" },
          { id = "buy_kit", label = kit_label, kind = "action", cost = 60 },
        },
      }
    end
    if page_id == "arenas" then
      return {
        title = "ARENAS",
        entries = {
          { id = "2", label = "THE SECOND", kind = "level", level = 2 },
          { id = "ghost", label = "GHOST", kind = "level", level = 9999 },
        },
      }
    end
    return { title = "EMPTY" }
  end,
  picker_action = function(entry_id)
    og.campaign_state_set("kit", 1)
    og.campaign_match_set("score_limit", 15)
    return { message = "Kit stowed for the road." }
  end,
}))LUA";

struct MissionsFlowState
{
    bool started = false;
    bool finished = false;
    bool missions_button_seen = false;
    bool missions_screen_opened = false;
    bool root_rows_seen = false;
    bool kit_label_flipped = false;
    bool arena_rows_seen = false;
    bool current_marker_seen = false;
    bool returned_to_root = false;
    bool scenario_resumed = false;
};

int missions_flow_injector(void* data)
{
    og::runtime::ensure_thread_session();
    MissionsFlowState* state = static_cast<MissionsFlowState*>(data);
    state->started = true;

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    // Base Camp -> SCENARIO.
    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);
    interact("scenario");

    // The MISSIONS door shows because the synthetic picker is registered.
    state->missions_button_seen = wait_for_interactable("missions", 10000);
    SDL_Delay(300);
    interact("missions");

    // The MISSIONS screen is up (its BACK owns the unique (10,169) rect).
    state->missions_screen_opened =
        wait_for_interactable_at("back", 10, 169, 10000);
    state->root_rows_seen =
        wait_for_interactable_label("mission_row_0", "ARENAS", 10000) &&
        wait_for_interactable_label("mission_row_1", "FIELD KIT  60g", 5000);
    SDL_Delay(300);
    capture_missions_frame("missions_root");

    // Buy the kit: debit + state write-through + page refetch relabel.
    interact("mission_row_1");
    state->kit_label_flipped =
        wait_for_interactable_label("mission_row_1", "KIT OWNED  60g", 5000);
    SDL_Delay(300);

    // Open the ARENAS page.
    interact("mission_row_0");
    state->arena_rows_seen =
        wait_for_interactable_label("mission_row_0", "THE SECOND", 10000) &&
        wait_for_interactable_label("mission_row_1", "GHOST", 5000);
    SDL_Delay(300);

    // The bogus level exercises the load-with-rollback branch (popup is
    // trace-only under TESTING; the flow continues).
    interact("mission_row_1");
    SDL_Delay(600);

    // The real level: the SDL level-set tail commits scen_num and the
    // refetched page decorates the row CURRENT.
    interact("mission_row_0");
    state->current_marker_seen = wait_for_interactable_label(
        "mission_row_0", "THE SECOND  [CURRENT]", 10000);
    SDL_Delay(300);
    capture_missions_frame("missions_arena_current");

    // BACK pops to the root page...
    interact("back");
    state->returned_to_root =
        wait_for_interactable_label("mission_row_0", "ARENAS", 10000);
    SDL_Delay(300);

    // ...and BACK at the root closes MISSIONS; SCENARIO resumes.
    interact("back");
    state->scenario_resumed = wait_for_interactable_at("back", 30, 170, 10000);
    SDL_Delay(300);
    interact("back");

    // Team build -> main menu.
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    state->finished = true;
    return 0;
}

} // namespace

TEST(CampaignMissionsUi, missions_flow_buys_pages_and_sets_level)
{
    trace_clear();
    SavedPickerSave save_guard;
    // Mount BEFORE registering: the save0 load inside picker_main then hits
    // the same-id mount no-op and never rescans the script registry.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    SyntheticCampaignScriptGuard script_guard;
    SyntheticCampaignScriptGuard::install(kMissionsScript);
    write_save0_with_two_soldiers("gladiator", 1);

    MissionsFlowState state;
    SDL_Thread* thread = SDL_CreateThread(
        missions_flow_injector, "missions_flow", &state);
    ASSERT_NE(nullptr, thread);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    SaveData& save = test_screen()->save_data;
    EXPECT_TRUE(state.finished) << "injector should complete the flow";
    EXPECT_TRUE(state.missions_button_seen)
        << "a registered picker shows the MISSIONS door";
    EXPECT_TRUE(state.missions_screen_opened);
    EXPECT_TRUE(state.root_rows_seen)
        << "root page rows compose label + cost";
    EXPECT_TRUE(state.kit_label_flipped)
        << "the action refetch re-derives the label from the decision book";
    EXPECT_TRUE(state.arena_rows_seen);
    EXPECT_TRUE(state.current_marker_seen)
        << "the level-set tail refetches with the CURRENT marker";
    EXPECT_TRUE(state.returned_to_root) << "BACK pops one session page";
    EXPECT_TRUE(state.scenario_resumed)
        << "BACK at the root closes MISSIONS into SCENARIO";

    // The SDL level-set tail committed the cursor.
    EXPECT_EQ(2, save.scen_num) << "the chosen level id landed in the save";
    // The action debited the acting team's wallet exactly once.
    EXPECT_EQ(4940u, save.m_totalcash[0]) << "5000 - 60g field kit";
    // og.campaign_state_set wrote through the installed providers into the
    // live save's per-campaign book.
    EXPECT_EQ(1, save.campaign_state_get("gladiator", "kit"));
    // #212: the action's og.campaign_match_set wrote the MATCHUP knob and
    // the Acted tail consumed the dirty flag into the settings sync.
    EXPECT_EQ(15, save.ctf_capture_limit)
        << "og.campaign_match_set writes through to the persisted knob";
    EXPECT_TRUE(trace_contains("missions", "match_settings_synced"))
        << "a match-setting write runs the sync-settings-from-save tail";

    // The bogus level refused with the standard rollback popup...
    EXPECT_TRUE(trace_contains("popup", "Invalid level file."))
        << "the ghost level takes the load-with-rollback branch";
    // ...and the tail traced both the denial-free sets.
    EXPECT_TRUE(trace_contains("missions", "level_set 2"));
    EXPECT_TRUE(trace_contains("missions", "acted"));
}

namespace {

// One shipped campaign's book tour: seed an interesting save, open
// MISSIONS, capture the root, then each listed root row's page. Row
// indexes are window-local (8 rows per page); -1 means "press next
// first". Captures are UXSHOTS_DIR-gated; without it the tour still
// walks every page, which keeps the shipped books' SDL rendering under
// integration coverage.
struct BookTourSpec
{
    const char* campaign;
    short scen_num;
    std::vector<int> completed;
    const char* root_shot;
    // {row index or -1 for the pager, shot name or nullptr for no capture}
    std::vector<std::pair<int, const char*>> pages;
    bool door_seen = false;
    bool opened = false;
    bool finished = false;
};

int book_tour_injector(void* data)
{
    og::runtime::ensure_thread_session();
    BookTourSpec* spec = static_cast<BookTourSpec*>(data);

    wait_for_interactable("continue_game", 5000);
    SDL_Delay(750);
    interact("continue_game");

    SDL_Delay(500);
    wait_for_interactable("scenario", 10000);
    SDL_Delay(750);
    interact("scenario");

    spec->door_seen = wait_for_interactable("missions", 10000);
    SDL_Delay(300);
    interact("missions");

    spec->opened = wait_for_interactable_at("back", 10, 169, 10000);
    wait_for_interactable("mission_row_0", 10000);
    SDL_Delay(500);
    capture_missions_frame(spec->root_shot);

    for (const auto& [row, shot] : spec->pages)
    {
        if (row < 0)
        {
            interact("next");
            SDL_Delay(500);
            if (shot != nullptr)
                capture_missions_frame(shot);
            continue;
        }
        interact("mission_row_" + std::to_string(row));
        wait_for_interactable("mission_row_0", 10000);
        SDL_Delay(500);
        if (shot != nullptr)
            capture_missions_frame(shot);
        interact("back");
        wait_for_interactable("mission_row_0", 10000);
        SDL_Delay(300);
    }

    // Close: MISSIONS root -> SCENARIO -> team build -> main menu.
    interact("back");
    wait_for_interactable_at("back", 30, 170, 10000);
    SDL_Delay(300);
    interact("back");
    wait_for_interactable("go", 10000);
    SDL_Delay(300);
    interact("back");

    spec->finished = true;
    return 0;
}

void run_book_tour(BookTourSpec& spec)
{
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error(spec.campaign));
    write_save0_with_two_soldiers(spec.campaign, spec.scen_num);
    SaveData& save = test_screen()->save_data;
    for (int level : spec.completed)
        save.add_level_completed(spec.campaign, level);
    ASSERT_TRUE(save.save("save0"));

    SDL_Thread* thread =
        SDL_CreateThread(book_tour_injector, "book_tour", &spec);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(spec.door_seen)
        << spec.campaign << ": the shipped pack must register a book";
    EXPECT_TRUE(spec.opened) << spec.campaign << ": MISSIONS must open";
    EXPECT_TRUE(spec.finished) << spec.campaign << ": tour must complete";
}

} // namespace

// The four shipped books, walked page by page in the real SDL picker.
// zz_ so the heavier picker sessions run after the focused flows.
TEST(CampaignMissionsUi, zz_capture_book_tour_of_the_shipped_campaigns)
{
    trace_clear();
    SavedPickerSave save_guard;

    BookTourSpec modes{"modes", 300,
                       {300, 301, 500},
                       "book_modes_root",
                       {{1, "book_modes_ctf"},
                        {7, "book_modes_card"},
                        {-1, nullptr},
                        {0, "book_modes_setup"}}};
    run_book_tour(modes);

    BookTourSpec westlands{"westlands", 10,
                           {1, 2, 3, 4, 5, 6, 7, 8, 9},
                           "book_fire_root",
                           {{0, "book_fire_road"},
                            {1, "book_fire_quartermaster"},
                            {2, "book_fire_ledger"}}};
    run_book_tour(westlands);

    BookTourSpec longseason{"longseason", 5,
                             {1, 2, 3, 4},
                             "book_kettle_root",
                             {{0, "book_kettle_coin"},
                              {1, "book_kettle_work"},
                              {2, "book_kettle_stores"},
                              {3, "book_kettle_debts"}}};
    run_book_tour(longseason);

    BookTourSpec dreams{"imaginations", 1, {}, "book_dreams_root", {}};
    run_book_tour(dreams);

    // Leave the default campaign mounted for whatever runs next.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
}

TEST(CampaignMissionsUi, scenario_button_hidden_without_registration)
{
    // No synthetic registration in scope: the production packs register no
    // campaign hooks, so the sync must hide MISSIONS.
    button* buttons = picker_scenariomenu_buttons();
    int count = picker_scenariomenu_button_count();
    ASSERT_EQ(kScenarioMenuButtonCount, count);
    int highlighted = 0;
    sync_scenario_menu_host_control_visibility(buttons, count, highlighted);
    EXPECT_TRUE(buttons[kScenarioMenuMissionsIndex].hidden)
        << "no registered picker => MISSIONS hidden";
    EXPECT_EQ(kScenarioMenuViewScenarioIndex,
              buttons[kScenarioMenuBackIndex].nav.up)
        << "BACK's up-link routes around the hidden MISSIONS";

    {
        SyntheticCampaignScriptGuard script_guard;
        SyntheticCampaignScriptGuard::install(kMissionsScript);
        buttons = picker_scenariomenu_buttons();
        count = picker_scenariomenu_button_count();
        highlighted = 0;
        sync_scenario_menu_host_control_visibility(buttons, count,
                                                   highlighted);
        EXPECT_FALSE(buttons[kScenarioMenuMissionsIndex].hidden)
            << "a registered picker shows MISSIONS";
        EXPECT_EQ(kScenarioMenuMissionsIndex,
                  buttons[kScenarioMenuBackIndex].nav.up);
    }
}
