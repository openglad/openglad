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
#include <openglad/resources/packs.h>
#include <openglad/resources/company.h>
#include <openglad/gameplay/lobby_state.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <optional>
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
Sint32 create_team_menu(Sint32 arg1);
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

// ---------------------------------------------------------------------------
// R2-3, the strip twin REVERSED: the Base Camp strip reads
// BACK - DIFFICULTY - SCENARIO - NETWORK - GO on EVERY campaign. Round 1
// swapped the second door for SETUP on a versus campaign because the
// wizard's RULES step had swallowed respawns, permadeath, generators,
// difficulty, gold and cross control. RULES is the two match knobs now, so
// those seven came home to the DIFFICULTY screen and the wizard's one SDL
// door is the docket's SETUP row inside the panel. No SETUP button on the
// strip, on any campaign.

namespace {

struct StripTwinState
{
    std::atomic<bool> test_finished{false};
    bool reached_base_camp = false;
    bool setup_visible = true;
    bool difficulty_visible = false;
    bool difficulty_screen_opened = false;
    bool respawn_row_visible = false;
    bool finished = false;
};

int strip_twin_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<StripTwinState*>(data);
    const auto escape = [state](int leg, const char* why) {
        // In precedence order: the DIFFICULTY subscreen this flow opens,
        // then Base Camp's own BACK, then the picker's way forward. The
        // subscreen publishes `difficulty_back`, NOT the shared `back` —
        // a table without it leaves the tail pressing nothing at all.
        static constexpr EscapeDoor kStripDoors[] = {
            {"difficulty_back", "difficulty_back"},
            {"back", "back"},
            {"go", "back"},
            {"continue_game", "continue_game"},
        };
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kStripDoors);
    };

    if (!wait_for_interactable("continue_game", 10000))
        return escape(1, "the main menu never came up");
    (void)wait_for_menu_frames(2);
    (void)interact("continue_game");
    state->reached_base_camp = wait_for_interactable("go", 15000);
    if (!state->reached_base_camp)
        return escape(2, "Base Camp never came up");
    // The strip is a per-frame rewire, so read it on a COMPLETED frame.
    (void)wait_for_menu_frames(2);
    state->setup_visible = has_interactable("setup");
    state->difficulty_visible = has_interactable("difficulty");
    // ...and the door is not just drawn: it opens the screen the seven
    // knobs came home to. RESPAWNS is one of them.
    state->difficulty_screen_opened =
        click_until_edge("difficulty", [](int wait_ms) {
            return wait_for_interactable("respawn_mode", wait_ms);
        });
    state->respawn_row_visible = has_interactable("respawn_mode");
    if (state->difficulty_screen_opened) {
        (void)click_until_edge("difficulty_back", [](int wait_ms) {
            return wait_for_interactable("go", wait_ms);
        });
    }
    state->finished = true;
    return escape(0, "");
}

} // namespace

TEST(Difficulty, versus_save_keeps_difficulty_on_the_strip)
{
    trace_clear();
    og::data::ScopedActiveCompany pin("save0");
    ASSERT_TRUE(pin.applied()) << "save0 must be a valid company slot";
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));

    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 820;
    save.numplayers = 1;
    save.current_campaign = "modes";
    ASSERT_TRUE(seed_open_company(save, "save0", newest_company_stamp() + 1))
        << "save0 must be seeded as the most recent company on disk";

    StripTwinState state;
    SDL_Thread* thread =
        SDL_CreateThread(strip_twin_injector, "strip_twin", &state);
    ASSERT_NE(nullptr, thread);
    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 1;
    picker_main(0, nullptr);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.reached_base_camp);
    EXPECT_TRUE(state.difficulty_visible)
        << "R2-3: DIFFICULTY is the strip's second door on EVERY campaign "
           "— respawns, permadeath, generators, gold and cross control "
           "live behind it again";
    EXPECT_FALSE(state.setup_visible)
        << "and the SETUP strip twin is gone: the wizard's one SDL door is "
           "the docket's SETUP row inside the panel";
    EXPECT_TRUE(state.difficulty_screen_opened)
        << "the door opens the screen, not just a face";
    EXPECT_TRUE(state.respawn_row_visible)
        << "RESPAWNS is one of the seven rules that came back from the "
           "wizard's RULES step";

    // Every other test in this binary expects the gladiator mount.
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    save.current_campaign = "gladiator";
    save.scen_num = 1;
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

// ---------------------------------------------------------------------------
// R2-D18: where a networked JOINER now sees CROSS CONTROL.
//
// Round 1 put a read-only CROSS CONTROL row on the wizard's RULES step.
// R2-3 shrank RULES to the two match knobs, so that row left with its
// knob — and the joiner's ONE sight of the mode that decides whether the
// host can drive their fighters is the Base Camp strip's DIFFICULTY door,
// which R2-3 also put back on every campaign. The row is deliberately NOT
// host-gated (kNetworkedOnlyGate) while its six siblings are, and a
// joiner's click answers with the host guard rather than a silent no-op.

namespace {

// A networked lobby this machine is a GUEST in: two seats, ours is the
// second. lobby_players() rebuilds per call, so the menu thread and the
// injector never share mutable vector storage.
class JoinerLobbyClient : public og::ui::IPickerLobbyClient
{
public:
    void initialize_from_save() override {}
    void shutdown() override {}
    void sync_from_save() override {}
    void sync_roster_from_save() override {}
    void sync_settings_from_save() override {}
    void poll_and_apply() override {}
    void set_player_mode(int) override {}
    bool request_start_game() override { return false; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    build_game_start_config() const override { return std::nullopt; }
    [[nodiscard]] std::optional<og::ui::PickerLobbyGameStartConfig>
    consume_game_start_config() override { return std::nullopt; }
    [[nodiscard]] bool start_request_pending() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool host_controls_visible() const noexcept override
    {
        return false;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }
    [[nodiscard]] std::vector<og::sim::LobbyPlayer>
    lobby_players() const override
    {
        og::sim::LobbyPlayer host;
        host.player_index = 0;
        host.name = "net-0000000000000000";
        host.company = "RED LANTERN";
        host.team = 0;
        host.is_host = true;
        og::sim::LobbyPlayer guest;
        guest.player_index = 1;
        guest.name = "net-0000000000000001";
        guest.company = "IRON KETTLE";
        guest.team = 1;
        return {host, guest};
    }
    [[nodiscard]] std::vector<std::uint8_t>
    local_player_indices() const override
    {
        return {1};
    }
};

struct JoinerLobbyGuard
{
    og::ui::IPickerLobbyClient* saved = nullptr;
    explicit JoinerLobbyGuard(og::ui::IPickerLobbyClient* client)
        : saved(og::ui::active_picker_lobby_client())
    {
        og::ui::install_active_picker_lobby_client(client);
    }
    ~JoinerLobbyGuard()
    {
        og::ui::install_active_picker_lobby_client(saved);
    }
};

struct JoinerCrossControlState
{
    std::atomic<bool> test_finished{false};
    bool camp_seen = false;
    bool screen_opened = false;
    bool cross_control_visible = false;
    bool host_rows_hidden = false;
    std::string hidden_witness;
    bool clicked = false;
    bool finished = false;
};

int joiner_cross_control_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<JoinerCrossControlState*>(data);
    const auto escape = [state](int leg, const char* why) {
        static constexpr EscapeDoor kJoinerDoors[] = {
            {"difficulty_back", "difficulty_back"},
            {"back", "back"},
            {"ready", "back"},
        };
        return escape_to_the_main_thread(state->test_finished, leg, why,
                                         kJoinerDoors);
    };

    // A joiner's strip wears READY where the host's wears GO.
    state->camp_seen = wait_for_interactable("ready", 15000);
    if (!state->camp_seen)
        return escape(1, "the joiner's Base Camp never came up");
    (void)wait_for_menu_frames(2);

    state->screen_opened = click_until_edge("difficulty", [](int wait_ms) {
        return wait_for_interactable("cross_control", wait_ms);
    });
    if (!state->screen_opened)
        return escape(2, "the DIFFICULTY door never opened for a joiner");
    (void)wait_for_menu_frames(2);
    state->cross_control_visible = has_interactable("cross_control");
    state->host_rows_hidden = true;
    for (const char* row : {"difficulty", "respawn_mode", "respawn_delay",
                            "permadeath", "generator_rate", "infinite_gold"})
    {
        if (has_interactable(row)) {
            state->host_rows_hidden = false;
            state->hidden_witness = row;
        }
    }

    // The click is a REFUSAL, so nothing it could acknowledge ever moves:
    // one press, and the guard's own trace is the oracle.
    state->clicked = interact("cross_control");
    (void)wait_for_menu_frames(2);

    (void)click_until_edge("difficulty_back", [](int wait_ms) {
        return wait_for_interactable("ready", wait_ms);
    });
    state->finished = true;
    return escape(0, "");
}

} // namespace

TEST(Difficulty, networked_joiner_sees_cross_control_read_only_on_versus)
{
    trace_clear();
    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("modes"));
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.scen_num = 820;
    save.numplayers = 1;
    save.current_campaign = "modes";
    save.cross_control = 0;
    ASSERT_TRUE(save.save("save0"));

    JoinerLobbyClient lobby;
    JoinerLobbyGuard lobby_guard(&lobby);

    JoinerCrossControlState state;
    SDL_Thread* thread = SDL_CreateThread(joiner_cross_control_injector,
                                          "joiner_cross", &state);
    ASSERT_NE(nullptr, thread);
    picker_load_menu_backdrops();
    create_team_menu(0);
    state.test_finished.store(true);
    int thread_result = 0;
    SDL_WaitThread(thread, &thread_result);
    escape_tail_join_hygiene();
    cleanup_picker_state();

    EXPECT_EQ(0, thread_result)
        << "the injector gave up at leg " << thread_result;
    ASSERT_TRUE(state.screen_opened)
        << "a joiner opens the strip's DIFFICULTY door too — reading the "
           "rules is the point";
    EXPECT_TRUE(state.cross_control_visible)
        << "R2-D18: CROSS CONTROL is the joiner's one sight of the mode "
           "that changes their rights, and it is not host-gated";
    EXPECT_TRUE(state.host_rows_hidden)
        << "every other row on this screen IS host-gated — '"
        << state.hidden_witness << "' must not be up for a joiner";
    EXPECT_TRUE(state.clicked);
    EXPECT_TRUE(trace_contains("teams", "cross_control_denied"))
        << "and the joiner's click answers with the host guard";
    EXPECT_TRUE(trace_contains("popup", "HOST CONTROLS THIS SETTING"))
        << "in words, not a silent no-op";
    EXPECT_EQ(0, static_cast<int>(save.cross_control))
        << "and it changes nothing";

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("gladiator"));
    save.current_campaign = "gladiator";
    save.scen_num = 1;
}
