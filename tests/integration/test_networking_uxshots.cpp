// LINEUP §6 NETWORKING visual smoke tests (the UxShots pattern): drive the
// real screen through picker_main / create_team_menu with injector threads,
// freeze one fully presented 320x200 frame per state, and assert it is
// nonblank. Set UXSHOTS_DIR to retain the frames as PPM artifacts; normal
// runs perform the smoke assertions without writing files.

#include "test_input_helpers.h"
#include "test_interact.h"
#include <SDL3/SDL.h>
#include <gtest/gtest.h>
#include <openglad/core/constants.h>
#include <openglad/core/test_trace.h>
#include <openglad/gameplay/guy.h>
#include <openglad/interface/button.h>
#include <openglad/interface/platform_bridge.h>
#include <openglad/interface/screen.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/interface/ui/picker_lobby_client.h>
#include <openglad/interface/ui/picker_ui_state.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../../src/interface/ui/picker_sdl_defs.h"

void picker_main(Sint32 argc, char** argv);
Sint32 create_team_menu(Sint32 arg1);
extern int g_picker_mainmenu_calls;
extern int g_picker_max_mainmenu_calls;
extern std::atomic_bool g_test_present_pause_requested;
extern std::atomic_bool g_test_present_paused;
void picker_testing_yes_or_no_queue_clear();
void picker_testing_set_force_real_dialogs(bool enabled);

static inline PickerState& pks()
{
    return *og::runtime::current_session->picker_;
}

namespace {

constexpr Uint64 kFramePauseTimeoutMs = 30000;

// The presenter handshake from test_uxshots_probe.cpp: freeze exactly one
// completed frame while its pixels are copied.
class PresentedFramePause
{
public:
    PresentedFramePause()
    {
        bool expected = false;
        if (!g_test_present_pause_requested.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel))
        {
            fprintf(stderr,
                    "  [netshot] FAILED: another capture already pending\n");
            abort();
        }
        const Uint64 deadline = SDL_GetTicks() + kFramePauseTimeoutMs;
        while (!g_test_present_paused.load(std::memory_order_acquire))
        {
            if (SDL_GetTicks() >= deadline)
            {
                fprintf(stderr,
                        "  [netshot] FAILED: no presented frame in time\n");
                abort();
            }
            SDL_Delay(1);
        }
        acquired_ = true;
    }

    ~PresentedFramePause()
    {
        if (!acquired_)
            return;
        g_test_present_pause_requested.store(false,
                                             std::memory_order_release);
        const Uint64 deadline = SDL_GetTicks() + kFramePauseTimeoutMs;
        while (g_test_present_paused.load(std::memory_order_acquire))
        {
            if (SDL_GetTicks() >= deadline)
            {
                fprintf(stderr, "  [netshot] presenter did not resume\n");
                abort();
            }
            SDL_Delay(1);
        }
    }

    PresentedFramePause(const PresentedFramePause&) = delete;
    PresentedFramePause& operator=(const PresentedFramePause&) = delete;

    [[nodiscard]] bool acquired() const { return acquired_; }

private:
    bool acquired_ = false;
};

bool capture_frame(const char* name)
{
    screen* scr = og::runtime::current_session->myscreen_;
    const char* output_dir = std::getenv("UXSHOTS_DIR");
    std::string path;
    std::vector<Uint8> rgb;
    if (output_dir != nullptr && output_dir[0] != '\0')
    {
        std::error_code error;
        std::filesystem::create_directories(output_dir, error);
        if (error)
        {
            fprintf(stderr, "  [netshot] FAILED to create %s\n", output_dir);
            return false;
        }
        path = std::string(output_dir) + "/" + name + ".ppm";
        rgb.reserve(320 * 200 * 3);
    }

    std::size_t nonblack_pixels = 0;
    {
        PresentedFramePause frame_pause;
        if (!frame_pause.acquired())
            return false;
        for (int y = 0; y < 200; ++y)
        {
            for (int x = 0; x < 320; ++x)
            {
                Uint8 r = 0, g = 0, b = 0;
                scr->get_pixel(x, y, &r, &g, &b);
                if (r != 0 || g != 0 || b != 0)
                    ++nonblack_pixels;
                if (!path.empty())
                {
                    rgb.push_back(r);
                    rgb.push_back(g);
                    rgb.push_back(b);
                }
            }
        }
    }

    if (!path.empty())
    {
        FILE* f = fopen(path.c_str(), "wb");
        if (f == nullptr)
        {
            fprintf(stderr, "  [netshot] FAILED to open %s\n", path.c_str());
            return false;
        }
        fprintf(f, "P6\n320 200\n255\n");
        fwrite(rgb.data(), sizeof(Uint8), rgb.size(), f);
        fclose(f);
        fprintf(stderr, "  [netshot] wrote %s\n", path.c_str());
    }
    fprintf(stderr, "  [netshot] %s: %zu nonblack pixels\n", name,
            nonblack_pixels);
    return nonblack_pixels >= 1000;
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

// A stub session client (the test_menu_layout / FakeNetLobbyClient pattern):
// enough surface for the NETWORKING session views and the Base Camp line B.
class ShotSessionLobbyClient final : public og::ui::IPickerLobbyClient
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
        return host_view.load();
    }
    [[nodiscard]] std::optional<std::string> connection_alert() const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return alert;
    }
    [[nodiscard]] std::vector<og::sim::LobbyPlayer> lobby_players()
        const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return players;
    }
    [[nodiscard]] std::vector<std::uint8_t> local_player_indices()
        const override
    {
        std::lock_guard<std::mutex> lock(mutex);
        return local_indices;
    }
    [[nodiscard]] bool is_networked_session() const noexcept override
    {
        return true;
    }
    // LINEUP §6: session mode needs an ESTABLISHED session, not merely a
    // networked client. These shots are of a live one.
    [[nodiscard]] bool session_established() const noexcept override
    {
        return true;
    }
    [[nodiscard]] std::string session_room_code() const override
    {
        return "GLAD-7Q2F";
    }
    bool kick_machine(og::sim::LobbyMachineId) override { return true; }

    std::atomic<bool> host_view{true};
    mutable std::mutex mutex;
    std::optional<std::string> alert;
    std::vector<og::sim::LobbyPlayer> players;
    std::vector<std::uint8_t> local_indices;
};

og::sim::LobbyPlayer shot_seat(std::uint8_t index,
                               og::sim::LobbyMachineId machine_id,
                               const char* name, const char* company,
                               bool is_host, bool ready)
{
    og::sim::LobbyPlayer player;
    player.player_index = index;
    player.seat_id = static_cast<og::sim::LobbySeatId>(index) + 1;
    player.machine_id = machine_id;
    player.name = name;
    player.company = company;
    player.is_host = is_host;
    player.ready = ready;
    return player;
}

// These shots seed save0 with a fresh company; leaving it behind reshuffles
// the CompanyList suite's positional row clicks (most-recent-first order),
// so each test reaps it on the way out — the probe's reap discipline.
void reap_save0_company()
{
    for (const og::data::CompanyBackupInfo& backup :
         og::data::list_company_backups("save0"))
        (void)og::data::delete_company_backup("save0", backup.seq);
    (void)remove_user_file("save/save0.gtl");
}

void seed_shot_save()
{
    SaveData& save = og::runtime::current_session->myscreen_->save_data;
    save.reset();
    save.save_name = "IRON KETTLE BAND";
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.numplayers = 1;
    save.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    save.team_list[0]->name = "GORT";
    save.team_list[0]->deployed = true;
    save.team_size = 1;
    ASSERT_EQ(SaveDataIoError::None, save.save_with_error("save0"));
}

struct NetShotState
{
    ShotSessionLobbyClient* lobby = nullptr;
    og::ui::IPickerLobbyClient* saved_client = nullptr;
    bool started = false;
    bool finished = false;
    int captures = 0;
};

bool open_networking_and_settle(const char* wait_id)
{
    SDL_Delay(300);
    interact("networking");
    if (!wait_for_interactable(wait_id, 10000))
        return false;
    SDL_Delay(1200); // settle: room-list status, labels, backdrop
    return true;
}

// One flow, four shots: idle reference, hosting PLAYERS view, the real KICK
// confirm dialog, and the joined view.
int networking_shots_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<NetShotState*>(data);
    state->started = true;

    if (!wait_for_interactable("continue_game", 10000))
    {
        state->finished = true;
        return 0;
    }
    SDL_Delay(750);
    interact("continue_game");
    if (!wait_for_interactable("networking", 10000))
    {
        state->finished = true;
        return 0;
    }

    // 1. Idle reference.
    if (open_networking_and_settle("network_back"))
        state->captures += capture_frame("networking_idle");
    SDL_Delay(300);
    interact("network_back");
    wait_for_interactable("networking", 10000);

    // 2. Hosting, two machines.
    run_on_main_thread([state] {
        state->saved_client = og::ui::active_picker_lobby_client();
        og::ui::install_active_picker_lobby_client(state->lobby);
        picker_testing_set_force_real_dialogs(true);
    });
    if (open_networking_and_settle("network_disconnect"))
    {
        state->captures += capture_frame("networking_hosting_two_machines");

        // 3. The real KICK confirm over the hosting view. Bounded retry:
        // under ASan frame-stretch a single click can be swallowed by the
        // menu transition (the click_until idiom from test_ctf_ui).
        SDL_Delay(300);
        for (int attempt = 0; attempt < 5 && !has_interactable("no");
             ++attempt)
        {
            interact("network_room_1");
            (void)wait_for_interactable("no", 2000);
        }
        if (wait_for_interactable("no", 10000))
        {
            SDL_Delay(750);
            state->captures += capture_frame("networking_kick_confirm");
            SDL_Delay(300);
            interact("no");
        }
        SDL_Delay(300);
        interact("network_back");
        wait_for_interactable("networking", 10000);
    }

    // 4. Joined view: same session, this machine is the joiner.
    run_on_main_thread([state] {
        picker_testing_set_force_real_dialogs(false);
        state->lobby->host_view.store(false);
        std::lock_guard<std::mutex> lock(state->lobby->mutex);
        state->lobby->local_indices = {1};
    });
    if (open_networking_and_settle("network_disconnect"))
        state->captures += capture_frame("networking_joined");
    SDL_Delay(300);
    interact("network_back");
    wait_for_interactable("networking", 10000);

    run_on_main_thread([state] {
        og::ui::install_active_picker_lobby_client(state->saved_client);
    });
    SDL_Delay(300);
    interact("back");
    if (wait_for_interactable("quit", 10000))
    {
        SDL_Delay(150);
        interact("quit");
    }
    state->finished = true;
    return 0;
}

} // namespace

TEST(NetworkingUxShots, session_views_and_kick_confirm)
{
    trace_clear();
    picker_testing_yes_or_no_queue_clear();
    seed_shot_save();

    // Scripted relay discovery: the idle reference shot shows two ACTIVE
    // GAMES rows without touching the real relay.
    struct BridgeGuard
    {
        PlatformBridge saved = platform_bridge();
        ~BridgeGuard() { set_platform_bridge(std::move(saved)); }
    } bridge_guard;
    PlatformBridge bridge = platform_bridge();
    bridge.begin_list_relay_rooms = {};
    bridge.list_relay_rooms =
        [](const std::string&, const std::string&)
            -> std::vector<og::ui::PickerRelayRoomInfo> {
        og::ui::PickerRelayRoomInfo alpha;
        alpha.code = "GLAD-AAAA";
        alpha.host_name = "Alpha";
        og::ui::PickerRelayRoomInfo bravo;
        bravo.code = "GLAD-BBBB";
        bravo.host_name = "Bravo";
        return {alpha, bravo};
    };
    set_platform_bridge(std::move(bridge));

    ShotSessionLobbyClient lobby;
    lobby.players = {
        shot_seat(0, 1, "net-self", "IRON KETTLE BAND", true, false),
        shot_seat(1, 2, "net-far", "RIVER BAND", false, true),
    };
    lobby.local_indices = {0};

    NetShotState state;
    state.lobby = &lobby;
    SDL_Thread* thread = SDL_CreateThread(networking_shots_injector,
                                          "networking_shots", &state);
    ASSERT_TRUE(thread != nullptr);

    g_picker_mainmenu_calls = 0;
    g_picker_max_mainmenu_calls = 2;
    picker_main(0, nullptr);
    SDL_WaitThread(thread, nullptr);
    cleanup_picker_state();
    g_picker_max_mainmenu_calls = 0;
    picker_testing_set_force_real_dialogs(false);
    reap_save0_company();

    ASSERT_TRUE(state.started);
    ASSERT_TRUE(state.finished);
    ASSERT_EQ(4, state.captures)
        << "idle, hosting, kick confirm and joined must all capture";
}

namespace {

struct LineBShotState
{
    bool finished = false;
    int captures = 0;
};

int basecamp_kicked_injector(void* data)
{
    og::runtime::ensure_thread_session();
    auto* const state = static_cast<LineBShotState*>(data);
    if (wait_for_interactable("networking", 20000))
    {
        SDL_Delay(1500);
        state->captures += capture_frame("basecamp_kicked_by_host");
        SDL_Delay(200);
        interact("back");
    }
    state->finished = true;
    return 0;
}

} // namespace

// The Base Camp line-B state a kicked joiner sees between the kick landing
// and the per-frame revert: connection_alert() == "KICKED BY HOST". The
// stub keeps the alert standing (the revert only swaps the OWNED client,
// never a test stub), so the frame is stable to capture.
TEST(NetworkingUxShots, basecamp_kicked_by_host_line_b)
{
    trace_clear();
    seed_shot_save();

    ShotSessionLobbyClient lobby;
    lobby.host_view.store(false);
    lobby.alert = "KICKED BY HOST";
    lobby.players = {
        shot_seat(0, 1, "net-host", "RIVER BAND", true, false),
        shot_seat(1, 2, "net-self", "IRON KETTLE BAND", false, false),
    };
    lobby.local_indices = {1};

    og::ui::IPickerLobbyClient* const saved_client =
        og::ui::active_picker_lobby_client();
    og::ui::install_active_picker_lobby_client(&lobby);

    LineBShotState state;
    SDL_Thread* thread = SDL_CreateThread(basecamp_kicked_injector,
                                          "netshot_kicked_lineb", &state);
    ASSERT_TRUE(thread != nullptr);
    picker_load_menu_backdrops();
    create_team_menu(0);
    SDL_WaitThread(thread, nullptr);
    og::ui::install_active_picker_lobby_client(saved_client);
    cleanup_picker_state();
    reap_save0_company();

    ASSERT_TRUE(state.finished);
    ASSERT_EQ(1, state.captures);
}
