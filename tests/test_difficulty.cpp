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
#include <openglad/resources/save_data.h>
#include <openglad/resources/io_common.h>
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
static inline PickerState& pks() { return *og::runtime::current_session->picker_; }


static void cleanup_picker_state()
{
    for (int i = 0; i < 5; i++) {
        pks().backdrops[i].reset();
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
// difficulty cycler plus the five match-rule settings.
//
// Flow: Main Menu -> DIFFICULTY door -> cycle every setting a full cycle
// (difficulty x3, respawns x3, delay x3, permadeath x2, generators x3,
// infinite gold x2 — all back to their defaults) -> BACK -> GAME SETTINGS
// -> CONTROLS -> BACK
// -> BACK -> return
//
// This used to tour PLAYER SETTINGS and all four player-count outlines here.
// Seat lifecycle now lives in Base Camp, while the persistent all-player
// controls door moved under GAME SETTINGS.
//
// Verifies:
//   1. The door opens the subscreen (its rows become interactable)
//   2. Every settings row is clickable and a full cycle restores defaults
//   3. BACK returns to a working main menu and CONTROLS returns cleanly

struct DifficultyState {
    bool started;
    bool finished;
    bool entered_submenu;
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
    if (!wait_for_interactable("control_settings", 5000))
        return 0;
    SDL_Delay(300);
    fprintf(stderr, "  [test] clicking CONTROLS\n");
    interact("control_settings");
    if (!wait_for_interactable("controls_back", 5000))
        return 0;
    SDL_Delay(300);
    EXPECT_TRUE(has_interactable("reset_all_controls"));
    interact("controls_back");
    if (!wait_for_interactable("options_back", 5000))
        return 0;
    SDL_Delay(300);
    interact("options_back");

    state->finished = true;
    return 0;
}

TEST(Difficulty, submenu_door_flow) {
    trace_clear();

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "org.openglad.gladiator";
    save.respawn_mode = 0;
    save.ctf_respawn_ticks = 0;
    save.keep_fallen_heroes = 0;
    save.generator_rate = 0;
    save.infinite_gold = 0;
    save.save("save0");
    og::runtime::current_session->current_difficulty_ = 1;

    DifficultyState state = { false, false, false, false };
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
