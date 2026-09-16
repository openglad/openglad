#include <memory>
#include <array>
#include <openglad/interface/button.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/render/pixien.h>
#include <openglad/core/test_trace.h>
#include <gtest/gtest.h>
#include <SDL3/SDL.h>
#include "test_company_cleanup.h"
#include "test_input_helpers.h"
#include "test_interact.h"
#include "test_click_ladder.h"
#include "test_escape_tail.h"
#include <openglad/resources/save_data.h>
#include <openglad/resources/io_common.h>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
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

// Test: the DIFFICULTY button on the Base Camp command strip is a DOOR into
// the blocking DIFFICULTY subscreen (unique BACK id "difficulty_back"), which
// holds the difficulty cycler plus the five match-rule settings.
//
// Flow: Main Menu -> CONTINUE -> Base Camp -> DIFFICULTY door -> cycle every
// setting a full cycle (difficulty x3, respawns x4, delay x3, permadeath x2,
// generators x3, infinite gold x2 — all back to their defaults) -> BACK ->
// Base Camp still live -> BACK -> main menu -> GAME SETTINGS -> BACK
//
// The door used to sit on the main menu; it moved to the camp with the rest
// of the "what is this fight like" controls (docs/camp-controls-design.md).
// The nested return is the point of the middle assertion: the subscreen's
// MENU_REDRAW has to be consumed by Base Camp's loop, not mistaken for an
// exit.
//
// This used to tour PLAYER SETTINGS and all four player-count outlines here,
// and later the global CONTROLS door. Seat lifecycle lives in Base Camp now,
// and per-player controls live on the seat's own player screen — GAME
// SETTINGS keeps only the game-wide rows.
//
// Verifies:
//   1. The strip door opens the subscreen (its rows become interactable)
//   2. Every settings row is clickable and a full cycle restores defaults
//   3. BACK returns to a LIVE Base Camp, whose own BACK reaches a working
//      main menu, where GAME SETTINGS opens and closes cleanly

struct DifficultyState {
    bool started = false;
    bool finished = false;
    bool entered_submenu = false;
    bool cycled_settings = false;
    bool reached_base_camp = false;
    bool returned_to_base_camp = false;
    // Set by the MAIN thread after picker_main returns, never by the
    // injector -- it is what tells the escape tail the menus are gone.
    std::atomic<bool> test_finished{false};
    // Point one leg at an id the flow never publishes, so the give-up path
    // itself is exercised by a committed test rather than by a defect.
    int sabotage_leg = 0;
};

// The sabotaged leg waits for an id nothing ever publishes, and clicks an id
// nothing answers, so the main thread stays exactly where that leg gave up.
// The window is short on purpose: nothing is coming, and the point of that
// run is the tail.
static constexpr int kSabotageWaitMs = 500;

static const char* leg_id(const DifficultyState* state, int leg, const char* id)
{
    return state->sabotage_leg == leg ? "never_published_row" : id;
}

static int leg_wait(const DifficultyState* state, int leg, int wait_ms)
{
    return state->sabotage_leg == leg ? kSabotageWaitMs : wait_ms;
}

// The value ladder that drives every cycler lap below -- click the row, prove
// the value it STORES moved on the menu thread, re-click on the documented
// 300 ms spacing, fail by name on the deadline -- lives in
// tests/test_click_ladder.h as click_until_value_moves(). It was written here
// and hoisted when the menu-capture scenes needed the same drive (PR #245:
// one implementation of a rule).

// The rows' stored values, all read on the menu thread by that ladder.
static SaveData& live_save()
{
    return og::runtime::current_session->myscreen_->save_data;
}

static int difficulty_injector(void* data)
{
    og::runtime::ensure_thread_session();
    DifficultyState* state = static_cast<DifficultyState*>(data);
    state->started = true;

    // Every give-up below goes through the shared escape tail
    // (tests/test_escape_tail.h) with a numbered leg. A bare `return 0` here
    // leaves the MAIN thread blocked inside picker_main with nothing left to
    // click it free, and the binary rides to the 420 s CTest ceiling instead
    // of naming the leg that quit.
    // Difficulty.a_leg_that_gives_up_frees_the_main_thread is the regression.
    const auto escape = [state](int leg, const char* why) {
        return escape_to_the_main_thread(state->test_finished, leg, why);
    };

    // The door lives in Base Camp now, so the flow starts with CONTINUE.
    wait_for_interactable("continue_game", 5000);
    wait_for_menu_frames(2);
    interact("continue_game");
    if (!wait_for_interactable(leg_id(state, 1, "difficulty"),
                               leg_wait(state, 1, 10000)))
        return escape(1, "Base Camp never showed the DIFFICULTY door");
    state->reached_base_camp = true;
    wait_for_menu_frames(2);

    // Open the DIFFICULTY door.
    fprintf(stderr, "  [test] clicking difficulty (door)\n");
    interact(leg_id(state, 2, "difficulty"));
    if (!wait_for_interactable(leg_id(state, 2, "difficulty_back"),
                               leg_wait(state, 2, 5000)))
        return escape(2, "the DIFFICULTY subscreen never appeared");
    state->entered_submenu = true;
    wait_for_menu_frames(2);

    // Full cycle on every row: each setting ends back at its default, and
    // EVERY individual click is verified against the value that row writes —
    // so an even number of lost clicks on a two-step lap (permadeath,
    // infinite gold) can no longer read as a completed cycle.
    click_until_value_moves("difficulty", 3,      // Battle -> Slaughter -> Skirmish -> Battle
                   [] { return static_cast<int>(
                            og::runtime::current_session->current_difficulty_); });
    click_until_value_moves("respawn_mode", 4,    // Off -> Heroes -> Everyone -> Team 1 -> Off
                   [] { return static_cast<int>(live_save().respawn_mode); });
    click_until_value_moves("respawn_delay", 3,   // Normal -> Fast -> Slow -> Normal
                   [] { return static_cast<int>(live_save().ctf_respawn_ticks); });
    click_until_value_moves("permadeath", 2,      // On -> Off -> On
                   [] { return static_cast<int>(live_save().keep_fallen_heroes); });
    click_until_value_moves("generator_rate", 3,  // Normal -> Calm -> Frenzy -> Normal
                   [] { return static_cast<int>(live_save().generator_rate); });
    click_until_value_moves("infinite_gold", 2,   // Off -> On -> Off
                   [] { return static_cast<int>(live_save().infinite_gold); });
    state->cycled_settings = true;

    // Leave the subscreen: the nested MENU_REDRAW must land back on a LIVE
    // Base Camp, not unwind it.
    fprintf(stderr, "  [test] clicking difficulty_back\n");
    interact("difficulty_back");
    if (!wait_for_interactable(leg_id(state, 3, "go"), leg_wait(state, 3, 5000)))
        return escape(3, "Base Camp did not survive the nested BACK");
    state->returned_to_base_camp = true;
    wait_for_menu_frames(2);
    EXPECT_TRUE(has_interactable("difficulty"))
        << "the strip door must still be live after its own subscreen closes";
    EXPECT_TRUE(has_interactable("scenario"))
        << "the whole strip must still be live after the nested BACK";

    // Base Camp's own BACK reaches the main menu, which no longer carries a
    // DIFFICULTY door of its own.
    fprintf(stderr, "  [test] clicking base camp back\n");
    interact("back");
    wait_for_interactable("continue_game", 5000);
    wait_for_menu_frames(2);

    EXPECT_FALSE(has_interactable("difficulty"))
        << "DIFFICULTY belongs to the Base Camp strip, not the main menu";
    EXPECT_FALSE(has_interactable("player_settings"))
        << "seat lifecycle belongs to the live Base Camp roster";
    fprintf(stderr, "  [test] clicking GAME SETTINGS\n");
    interact("options");
    if (!wait_for_interactable(leg_id(state, 4, "options_back"),
                               leg_wait(state, 4, 5000)))
        return escape(4, "GAME SETTINGS never opened");
    wait_for_menu_frames(2);
    // Player controls are per-seat now: GAME SETTINGS must not carry the
    // retired global CONTROLS door or its RESET ALL.
    EXPECT_FALSE(has_interactable("control_settings"));
    EXPECT_FALSE(has_interactable("reset_all_controls"));
    EXPECT_TRUE(has_interactable("game_speed"));
    interact("options_back");

    state->finished = true;
    // The flow's last BACK belongs to the tail for the same reason every
    // give-up does: picker_main returns under it, and after that return
    // there is no pump left to service a ladder's acknowledge post.
    return escape(0, "");
}

TEST(Difficulty, submenu_door_flow) {
    trace_clear();

    // CONTINUE opens the MOST RECENT company on disk, not the one this test
    // wrote: a bare SaveData::save() never stamps last_played_unix_s, so a
    // company another test founded would take the session over silently.
    // Seed through the autosave choke point that stamps, and check after the
    // flow which company it actually got.
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    save.respawn_mode = 0;
    save.ctf_respawn_ticks = 0;
    save.keep_fallen_heroes = 0;
    save.generator_rate = 0;
    save.infinite_gold = 0;
    ASSERT_TRUE(seed_open_company(save, "save0", newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";
    og::runtime::current_session->current_difficulty_ = 1;

    DifficultyState state;
    SDL_Thread* thread = SDL_CreateThread(difficulty_injector, "difficulty_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    // Two passes: the first opens Base Camp, the second is the main menu the
    // camp's BACK returns to (where GAME SETTINGS is checked).
    g_picker_max_mainmenu_calls = 2;

    picker_main(0, nullptr);
    state.test_finished.store(true);

    int thread_result;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;

    // The flow's whole claim is about THIS company's settings. CONTINUE
    // opens the most recent company on disk, not the one a test happened to
    // write, so without this line the whole flow can run on — and autosave
    // into — a company some other test founded, and still report green.
    ASSERT_EQ("save0", og::data::active_company_slot())
        << "the flow must have run on the company this test seeded";

    ASSERT_TRUE(state.finished) << "injector thread should have completed";
    ASSERT_TRUE(state.reached_base_camp)
        << "CONTINUE should reach a Base Camp carrying the DIFFICULTY door";
    ASSERT_TRUE(state.entered_submenu) << "DIFFICULTY door should open the subscreen";
    ASSERT_TRUE(state.cycled_settings) << "should have cycled every setting";
    ASSERT_TRUE(state.returned_to_base_camp)
        << "the subscreen's BACK should return to a live Base Camp";

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

// The regression for the wedge itself, and the reason every give-up above is
// spelled `return escape(N, ...)`.
//
// Leg 2 is the worst arm in this file: the main thread is already INSIDE Base
// Camp when the leg quits, and the sabotage points both the CLICK and the
// EDGE at an id nothing publishes, so no subscreen the tail cannot close is
// left open. The tail then has to walk BACK -> main menu until the iteration
// cap makes present_menu answer Quit; only then does picker_main return and
// SDL_WaitThread join.
//
// Restore the bare `return 0;` this arm carried before PR #292 and this test
// hangs to the 420 s CTest ceiling instead of naming the leg.
TEST(Difficulty, a_leg_that_gives_up_frees_the_main_thread)
{
    trace_clear();

    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 1;
    save.numplayers = 1;
    save.current_campaign = "gladiator";
    ASSERT_TRUE(seed_open_company(save, "save0", newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";

    DifficultyState state;
    state.sabotage_leg = 2;
    SDL_Thread* thread =
        SDL_CreateThread(difficulty_injector, "difficulty_escape_test", &state);
    ASSERT_TRUE(thread != nullptr) << "failed to create injector thread";

    g_picker_mainmenu_calls = 0;
    // One capped pass: leg 2 gives up with the main thread inside Base Camp,
    // so the tail's single BACK is all it takes to reach the cap.
    g_picker_max_mainmenu_calls = 1;

    picker_main(0, nullptr);
    state.test_finished.store(true);

    int thread_result = -1;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();

    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_TRUE(state.started) << "the injector thread never ran";
    EXPECT_EQ(2, thread_result)
        << "the sabotaged leg must be reported by number, not swallowed";
    EXPECT_TRUE(state.reached_base_camp)
        << "leg 2 is the deep arm: it only fires after Base Camp published "
           "the DIFFICULTY door";
    EXPECT_FALSE(state.entered_submenu)
        << "a flow that gave up at leg 2 never opened the subscreen";
    EXPECT_FALSE(state.cycled_settings)
        << "a flow that gave up at leg 2 never cycled a row";
    EXPECT_FALSE(state.finished)
        << "a flow that gave up at leg 2 never completed";
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
