#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_input_helpers.h"
#include "test_interact.h"
#include "test_save_state_guard.h"
#include <openglad/resources/save_data.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/gparser.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
// myscreen is now a macro defined in base.h (via game_session.h)

// Forward declarations from picker.cpp
void picker_main(Sint32 argc, char **argv);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;

#include <openglad/interface/ui/picker_ui_state.h>
#include "../../src/interface/ui/picker_sdl_defs.h"
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }

namespace {

class ScopedCfgMaps
{
public:
    ScopedCfgMaps()
        : data_(cfg.data), overrides_(cfg.overrides)
    {
    }

    ~ScopedCfgMaps()
    {
        cfg.data = std::move(data_);
        cfg.overrides = std::move(overrides_);
    }

private:
    decltype(cfg.data) data_;
    decltype(cfg.overrides) overrides_;
};

} // namespace


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

// Test: The main-menu DIFFICULTY button is a DOOR into the blocking
// DIFFICULTY subscreen (unique BACK id "difficulty_back"), which holds the
// difficulty cycler plus the six match-rule settings.
//
// Flow: Main Menu -> DIFFICULTY door -> cycle every setting a full cycle
// (difficulty x3, respawns x3, delay x3, permadeath x2, generators x3,
// infinite gold x2, dynamics x2 — all back to their defaults) -> BACK -> GAME SETTINGS
// -> BACK -> return
//
// This used to tour PLAYER SETTINGS and all four player-count outlines here,
// and later the global CONTROLS door. Seat lifecycle lives in Base Camp now,
// and per-player controls live on the seat's own player screen — GAME
// SETTINGS keeps only the game-wide rows.
//
// Verifies:
//   1. The door opens the subscreen (its rows become interactable)
//   2. Every settings row is clickable and a full cycle restores defaults
//   3. BACK returns to a working main menu, and GAME SETTINGS opens and
//      closes cleanly after it

struct DifficultyState {
    bool started;
    bool finished;
    bool entered_submenu;
    bool saw_classic_dynamics;
    bool cycled_settings;
};

// Click `id` `times` times, spacing the clicks so each press/release pair is
// consumed before the next (a second press without a release is dropped).
static void interact_times(const std::string& id, int times)
{
    for (int i = 0; i < times; ++i) {
        fprintf(stderr, "  [test] clicking %s (%d/%d)\n", id.c_str(), i + 1, times);
        interact(id);
        SDL_Delay(300);
    }
}

static bool wait_for_interactable_label(const std::string& id,
                                        const std::string& expected,
                                        int timeout_ms = 5000)
{
    constexpr int poll_interval_ms = 50;
    for (int elapsed = 0; elapsed < timeout_ms;
         elapsed += poll_interval_ms)
    {
        for (const Interactable& item : get_interactables())
        {
            if (item.id == id && !item.hidden && item.label == expected)
                return true;
        }
        SDL_Delay(poll_interval_ms);
    }
    fprintf(stderr,
            "  [test] TIMEOUT waiting for '%s' label '%s' (%d ms)\n",
            id.c_str(), expected.c_str(), timeout_ms);
    return false;
}

static int difficulty_injector(void* data)
{
    og::runtime::ensure_thread_session();
    DifficultyState* state = static_cast<DifficultyState*>(data);
    state->started = true;

    wait_for_interactable("difficulty", 5000);
    SDL_Delay(750);

    // Open the DIFFICULTY door.
    fprintf(stderr, "  [test] clicking difficulty (door)\n");
    interact("difficulty");
    if (!wait_for_interactable("difficulty_back", 5000)) {
        fprintf(stderr, "  [test] DIFFICULTY subscreen never appeared\n");
        return 0;
    }
    state->entered_submenu = true;
    SDL_Delay(300);

    // Full cycle on every row: each setting ends back at its default.
    interact_times("difficulty", 3);     // Battle -> Slaughter -> Skirmish -> Battle
    interact_times("respawn_mode", 4);   // Off -> Heroes -> Everyone -> Team 1 -> Off
    interact_times("respawn_delay", 3);  // Normal -> Fast -> Slow -> Normal
    interact_times("permadeath", 2);     // On -> Off -> On
    interact_times("generator_rate", 3); // Normal -> Calm -> Frenzy -> Normal
    interact_times("infinite_gold", 2);  // Off -> On -> Off
    fprintf(stderr, "  [test] toggling dynamics to Classic\n");
    interact("dynamics");
    const bool live_classic_label =
        wait_for_interactable_label("dynamics", "Classic Pace");
    std::string descriptor_label;
    {
        AllButtonsLock lock;
        descriptor_label =
            pks().difficulty_menu_buttons[kDifficultyMenuDynamicsIndex].label;
    }
    const SaveData& classic_save =
        og::runtime::current_session->myscreen_->save_data;
    state->saw_classic_dynamics =
        live_classic_label &&
        classic_save.dynamics_ruleset == og::sim::DynamicsRuleset::Classic &&
        cfg.dynamics_ruleset_preference() ==
            og::sim::DynamicsRuleset::Classic &&
        og::runtime::current_session->myscreen_->world().dynamics_ruleset ==
            og::sim::DynamicsRuleset::Classic &&
        descriptor_label == "Classic Pace";
    EXPECT_TRUE(state->saw_classic_dynamics)
        << "the first click must update the live row, descriptor, preference, "
           "SaveData, and GameWorld to Classic";

    // The label changes on button-down; wait through the matching release so
    // the next press is a distinct edge instead of being dropped as held.
    SDL_Delay(300);
    fprintf(stderr, "  [test] toggling dynamics back to Modern\n");
    interact("dynamics");
    EXPECT_TRUE(wait_for_interactable_label("dynamics", "Modern Pace"));
    SDL_Delay(300);
    state->cycled_settings = true;

    // Leave the subscreen.
    fprintf(stderr, "  [test] clicking difficulty_back\n");
    interact("difficulty_back");
    wait_for_interactable("continue_game", 5000);
    SDL_Delay(300);

    EXPECT_FALSE(has_interactable("player_settings"))
        << "seat lifecycle belongs to the live Base Camp roster";
    fprintf(stderr, "  [test] clicking GAME SETTINGS\n");
    interact("options");
    if (!wait_for_interactable("options_back", 5000))
        return 0;
    SDL_Delay(300);
    // Player controls are per-seat now: GAME SETTINGS must not carry the
    // retired global CONTROLS door or its RESET ALL.
    EXPECT_FALSE(has_interactable("control_settings"));
    EXPECT_FALSE(has_interactable("reset_all_controls"));
    EXPECT_TRUE(has_interactable("game_speed"));
    interact("options_back");

    state->finished = true;
    return 0;
}

TEST(Difficulty, submenu_door_flow) {
    trace_clear();
    og::test::ScopedPhysicalFileState config_file(
        std::filesystem::path(get_user_path()) / "cfg" / "openglad.yaml");
    ASSERT_TRUE(config_file.ready()) << config_file.error().message();
    ScopedCfgMaps restore_cfg;

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    save.respawn_mode = 0;
    save.ctf_respawn_ticks = 0;
    save.keep_fallen_heroes = 0;
    save.generator_rate = 0;
    save.infinite_gold = 0;
    save.dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    cfg.set_dynamics_ruleset_preference(og::sim::DynamicsRuleset::Modern);
    save.save("save0");
    og::runtime::current_session->current_difficulty_ = 1;

    DifficultyState state = { false, false, false, false, false };
    SDL_Thread* thread = SDL_CreateThread(difficulty_injector, "difficulty_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.entered_submenu) << "DIFFICULTY door should open the subscreen";
    ASSERT_TRUE(state.saw_classic_dynamics)
        << "the first dynamics click should select Classic exactly";
    ASSERT_TRUE(state.cycled_settings) << "should have cycled every setting";

    // Full cycles restore every setting to its default.
    SaveData& after = og::runtime::current_session->myscreen_->save_data;
    EXPECT_EQ(1, og::runtime::current_session->current_difficulty_)
        << "difficulty should be back at Battle after a full cycle";
    EXPECT_EQ(0, after.respawn_mode) << "respawns should be back at Off";
    EXPECT_EQ(0, after.ctf_respawn_ticks) << "respawn delay should be back at Normal";
    EXPECT_EQ(0, after.keep_fallen_heroes) << "permadeath should be back at On";
    EXPECT_EQ(0, after.generator_rate) << "generators should be back at Normal";
    EXPECT_EQ(0, after.infinite_gold) << "infinite gold should be back at Off";
    EXPECT_EQ(og::sim::DynamicsRuleset::Modern, after.dynamics_ruleset)
        << "dynamics should be back at Modern";
    EXPECT_EQ(og::sim::DynamicsRuleset::Modern,
              cfg.dynamics_ruleset_preference());
    EXPECT_EQ(og::sim::DynamicsRuleset::Modern,
              og::runtime::current_session->myscreen_->world()
                  .dynamics_ruleset);
}

TEST(Difficulty, dynamics_ruleset_never_reaches_the_save_file)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const auto dynamics_before = save.dynamics_ruleset;

    const std::filesystem::path save_dir =
        std::filesystem::path(get_user_path()) / "save";
    const std::filesystem::path classic_path =
        save_dir / "test_dynamics_classic.gtl";
    const std::filesystem::path modern_path =
        save_dir / "test_dynamics_modern.gtl";

    save.dynamics_ruleset = og::sim::DynamicsRuleset::Classic;
    ASSERT_TRUE(save.save("test_dynamics_classic"));
    save.dynamics_ruleset = og::sim::DynamicsRuleset::Modern;
    ASSERT_TRUE(save.save("test_dynamics_modern"));

    const auto read_all = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::vector<char>((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    };
    const std::vector<char> classic_bytes = read_all(classic_path);
    const std::vector<char> modern_bytes = read_all(modern_path);
    ASSERT_FALSE(classic_bytes.empty());
    EXPECT_EQ(classic_bytes.size(), modern_bytes.size());
    EXPECT_EQ(classic_bytes, modern_bytes)
        << "session dynamics must not change any company byte";

    SaveData reopened;
    ASSERT_TRUE(reopened.load("test_dynamics_classic"));
    EXPECT_EQ(og::sim::DynamicsRuleset::Modern,
              reopened.dynamics_ruleset)
        << "a file cannot override the fresh frontend default";

    save.dynamics_ruleset = og::sim::DynamicsRuleset::Classic;
    ASSERT_TRUE(save.load("test_dynamics_modern"));
    EXPECT_EQ(og::sim::DynamicsRuleset::Classic,
              save.dynamics_ruleset)
        << "in-place load preserves the live session carrier";

    save.dynamics_ruleset = dynamics_before;
    std::error_code ec;
    std::filesystem::remove(classic_path, ec);
    std::filesystem::remove(modern_path, ec);
}

// Infinite gold is a SESSION-ONLY setting: the wallet is never inflated and
// the flag itself never reaches the .gtl bytes. Saving with it on must
// produce a byte-identical file to saving with it off, and a load must never
// bring it back — this is what keeps every company autosave from baking the
// cheat into the player's file.
TEST(Difficulty, infinite_gold_never_reaches_the_save_file)
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    const short gold_before = save.infinite_gold;

    const std::filesystem::path save_dir =
        std::filesystem::path(get_user_path()) / "save";
    const std::filesystem::path off_path = save_dir / "test_gold_off.gtl";
    const std::filesystem::path on_path = save_dir / "test_gold_on.gtl";

    save.infinite_gold = 0;
    ASSERT_TRUE(save.save("test_gold_off")) << "control save should succeed";
    save.infinite_gold = 1;
    ASSERT_TRUE(save.save("test_gold_on")) << "cheat-on save should succeed";

    const auto read_all = [](const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        return std::vector<char>((std::istreambuf_iterator<char>(in)),
                                 std::istreambuf_iterator<char>());
    };
    const std::vector<char> off_bytes = read_all(off_path);
    const std::vector<char> on_bytes = read_all(on_path);
    ASSERT_FALSE(off_bytes.empty()) << "control save should have been written";
    EXPECT_EQ(off_bytes.size(), on_bytes.size())
        << "infinite_gold must not add bytes to the GTL format";
    EXPECT_TRUE(off_bytes == on_bytes)
        << "infinite_gold must not change a single serialized byte";

    // A company opened into a fresh SaveData always starts on the classic
    // economy: nothing in the file can turn the cheat back on. (An in-place
    // load leaves the live session's own toggle alone, exactly like
    // cross_control — the flag belongs to the session, not the file.)
    SaveData reopened;
    ASSERT_TRUE(reopened.load("test_gold_on")) << "load should succeed";
    EXPECT_EQ(0, static_cast<int>(reopened.infinite_gold))
        << "a loaded company always starts on the classic economy";

    save.infinite_gold = gold_before;
    std::error_code ec;
    std::filesystem::remove(off_path, ec);
    std::filesystem::remove(on_path, ec);
}
