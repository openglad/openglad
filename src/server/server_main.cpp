#include <openglad/core/constants.h>
#include <openglad/core/frame_rate_config.h>
#include <openglad/core/util.h>
#include <openglad/core/weather.h>
#include <openglad/gameplay/family_registries.h>
#include <openglad/gameplay/game_server.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/gameplay_context.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/net_constants.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/interface/session_state.h>
#include <openglad/platform/game_context.h>
#include <openglad/platform/net_transport_websocket_server.h>
#include <openglad/resources/campaign_io.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/level_data_hooks.h>
#include <openglad/resources/save_data.h>
#include <openglad/server/headless_server_runtime.h>
#include <openglad/server/headless_tick_interval.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

void io_init(int argc, char* argv[]);
void io_exit();

namespace og::runtime {

static SessionState s_headless_session{};
thread_local SessionState* current_session = &s_headless_session;
std::atomic<SessionState*> primary_session{&s_headless_session};
std::atomic<GameplayContext*> primary_game{&s_headless_session.game_};

} // namespace og::runtime

void popup_dialog(const char* title, const char* message)
{
    std::fprintf(stderr, "[%s] %s\n", title, message);
}

std::uint32_t random(std::uint32_t x)
{
    static std::uint32_t state = 12345;
    if (x == 0)
        return 0;
    state = state * 1103515245u + 12345u;
    return (state >> 16) % x;
}

namespace {

constexpr int kDefaultPort = 12345;
constexpr int kDefaultLobbyPollMs = 10;
std::atomic<bool> g_shutdown_requested = false;

struct ServerArgs {
    std::string host = "127.0.0.1";
    int port = kDefaultPort;
    int lobby_poll_ms = kDefaultLobbyPollMs;
    std::optional<int> target_fps;
};

void request_shutdown(int)
{
    g_shutdown_requested.store(true, std::memory_order_release);
}

void print_usage()
{
    std::fprintf(
        stderr,
        "Usage: openglad_server [options]\n"
        "  --host <addr>         Listen address (default: 127.0.0.1)\n"
        "  --port <num>          Listen port (default: 12345)\n"
        "  --lobby-poll-ms <n>   Lobby poll interval in ms (default: 10)\n"
        "  --fps <n>             Deprecated; no longer affects sim cadence (master semantics derived from world.timer_wait).\n"
        "  --help, -h            Show this help message\n");
}

bool parse_int_arg(const char* text, int& out)
{
    if (text == nullptr || text[0] == '\0')
        return false;

    char* end = nullptr;
    const long parsed = std::strtol(text, &end, 10);
    if (end == nullptr || *end != '\0')
        return false;
    if (parsed < std::numeric_limits<int>::min() ||
        parsed > std::numeric_limits<int>::max())
    {
        return false;
    }

    out = static_cast<int>(parsed);
    return true;
}

enum class ParseArgsResult {
    Ok,
    HelpRequested,
    InvalidUsage,
};

ParseArgsResult parse_args(int argc, char* argv[], ServerArgs& args)
{
    for (int index = 1; index < argc; ++index)
    {
        const std::string_view arg = argv[index];
        if (arg == "--host")
        {
            if (index + 1 >= argc)
            {
                std::fprintf(stderr, "--host requires a value\n");
                print_usage();
                return ParseArgsResult::InvalidUsage;
            }
            args.host = argv[++index];
            continue;
        }

        if (arg == "--port")
        {
            if (index + 1 >= argc ||
                !parse_int_arg(argv[index + 1], args.port) ||
                args.port <= 0)
            {
                std::fprintf(stderr, "--port requires a positive integer\n");
                print_usage();
                return ParseArgsResult::InvalidUsage;
            }
            ++index;
            continue;
        }

        if (arg == "--lobby-poll-ms")
        {
            if (index + 1 >= argc ||
                !parse_int_arg(argv[index + 1], args.lobby_poll_ms) ||
                args.lobby_poll_ms < 0)
            {
                std::fprintf(stderr, "--lobby-poll-ms requires a non-negative integer\n");
                print_usage();
                return ParseArgsResult::InvalidUsage;
            }
            ++index;
            continue;
        }

        if (arg == "--fps")
        {
            int fps = 0;
            if (index + 1 >= argc ||
                !parse_int_arg(argv[index + 1], fps) ||
                fps <= 0)
            {
                std::fprintf(stderr,
                             "--fps requires a positive integer\n");
                print_usage();
                return ParseArgsResult::InvalidUsage;
            }
            args.target_fps = fps;
            ++index;
            continue;
        }

        if (arg == "--help" || arg == "-h")
        {
            print_usage();
            return ParseArgsResult::HelpRequested;
        }

        std::fprintf(stderr, "Unknown argument: %.*s\n",
                     static_cast<int>(arg.size()),
                     arg.data());
        print_usage();
        return ParseArgsResult::InvalidUsage;
    }

    return ParseArgsResult::Ok;
}

class GameplayActiveScope
{
public:
    explicit GameplayActiveScope(og::runtime::SessionState& session)
        : session_(session)
        , previous_(session.gameplay_active_)
    {
        session_.gameplay_active_ = true;
    }

    ~GameplayActiveScope()
    {
        session_.gameplay_active_ = previous_;
    }

    GameplayActiveScope(const GameplayActiveScope&) = delete;
    GameplayActiveScope& operator=(const GameplayActiveScope&) = delete;

private:
    og::runtime::SessionState& session_;
    bool previous_ = false;
};

} // namespace

int main(int argc, char* argv[])
{
    // One-shot clock sample for the per-level weather roll nonce (defaults
    // to 0 in test builds, which link this runtime without this main).
    og::set_weather_roll_nonce(static_cast<std::uint32_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));

    ServerArgs args;
    switch (parse_args(argc, argv, args))
    {
    case ParseArgsResult::Ok:
        break;
    case ParseArgsResult::HelpRequested:
        return EXIT_SUCCESS;
    case ParseArgsResult::InvalidUsage:
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, request_shutdown);
#ifdef SIGTERM
    std::signal(SIGTERM, request_shutdown);
#endif

    bool io_initialized = false;
    try
    {
        io_init(argc, argv);
        io_initialized = true;

        cfg.load_settings();
        if (args.target_fps.has_value())
        {
            og::core::apply_target_fps_to_cfg(cfg, *args.target_fps);
        }
        std::uint32_t frame_interval_ms =
            og::server::compute_headless_tick_interval_ms(
                og::sim::DEFAULT_TIMER_WAIT);
        init_all_registries();

        og::runtime::SessionState& session = og::runtime::s_headless_session;
        ProductionRandom production_rng;
        session.ctx_.rng = &production_rng;
        session.game_.sim_events = session.ctx_.sim_events.get();
        session.game_.config = &cfg;
        session.game_.session_rng_ref = &session.ctx_.rng;
        session.game_.gameplay_active_ref = &session.gameplay_active_;
        og::runtime::primary_game.store(&session.game_, std::memory_order_release);

        og::sim::WebSocketServerTransport::Options transport_options;
        transport_options.host = args.host;
        og::sim::WebSocketServerTransport transport(args.port, transport_options);
        transport.accept_connections();
        Log("headless_server_listening host={} port={}\n", args.host, args.port);

        og::sim::LobbyServer lobby_server(transport);
        while (!g_shutdown_requested.load(std::memory_order_acquire))
        {
            lobby_server.poll_incoming_messages();
            if (lobby_server.consume_start_game_requested())
                break;

            std::this_thread::sleep_for(
                std::chrono::milliseconds(args.lobby_poll_ms));
        }

        if (g_shutdown_requested.load(std::memory_order_acquire))
        {
            if (io_initialized)
                io_exit();
            return 0;
        }

        const og::sim::LobbySaveDataEquivalent lobby_save =
            lobby_server.build_save_data_equivalent();
        const std::vector<og::sim::LobbyPlayerBinding> player_bindings =
            lobby_server.build_player_bindings();

        SaveData active_save;
        og::server::apply_headless_lobby_game_start_config(active_save, lobby_save);
        SaveData checkpoint_save;
        og::server::copy_headless_server_save_data(checkpoint_save, active_save);

        session.current_difficulty_ = static_cast<std::int32_t>(
            lobby_server.state().settings.difficulty);

        LevelRuntimeData level_data(
            active_save.scen_num,
            true,
            &headless_level_data_hooks());
        level_data.set_sim_context(
            &active_save,
            &level_data.world().enemy_freeze,
            session.ctx_.sim_events.get(),
            session.ctx_.rng,
            &cfg);
        session.game_.world = &level_data.world();
        session.game_.save = &active_save;

        GameplayActiveScope gameplay_active(session);
        GameplayContextGuard gameplay_guard(&session.game_);
        if (!og::server::load_headless_level_from_save(
                level_data,
                active_save,
                session.current_difficulty_,
                *session.ctx_.sim_events,
                /*authoritative=*/true))
        {
            if (io_initialized)
                io_exit();
            return 1;
        }

        // §4.4 control-policy install: derive owner-locked from the
        // negotiated lobby config (session-only cross_control rides the
        // equivalent) and stamp the machine map BEFORE bind_player scans run
        // below; snapshot v9 replicates both scalars to every joiner mirror.
        og::sim::install_control_policy(level_data.world(),
                                        /*networked=*/true,
                                        active_save.cross_control != 0,
                                        player_bindings,
                                        active_save.team_list);

        og::sim::GameServer game_server(
            level_data.world(),
            *session.ctx_.sim_events,
            transport);
        for (const og::sim::PeerId peer_id : transport.connected_peers())
            game_server.connect_client(peer_id);
        for (const og::sim::LobbyPlayerBinding& binding : player_bindings)
        {
            game_server.bind_player(binding.peer_id,
                                    binding.player_index,
                                    static_cast<short>(binding.team),
                                    nullptr,
                                    binding.local_slot);
        }

        game_server.on_save_sync = [&active_save, &level_data] {
            og::server::sync_headless_server_save_data_from_world(
                active_save, level_data.world());
        };
        game_server.on_level_transition =
            [&level_data,
             &active_save,
             &checkpoint_save,
             &session](int next_level) {
                return og::server::complete_headless_level_and_load_next(
                    level_data,
                    active_save,
                    checkpoint_save,
                    session.current_difficulty_,
                    *session.ctx_.sim_events,
                    next_level);
            };
        game_server.on_exit_accepted =
            [&level_data,
             &active_save,
             &checkpoint_save,
             &session](int destination) {
                return og::server::complete_headless_level_and_load_next(
                    level_data,
                    active_save,
                    checkpoint_save,
                    session.current_difficulty_,
                    *session.ctx_.sim_events,
                    destination);
            };
        game_server.on_withdraw_accepted =
            [&level_data,
             &active_save,
             &checkpoint_save,
             &session](int destination) {
                return og::server::withdraw_headless_level(
                    level_data,
                    active_save,
                    checkpoint_save,
                    session.current_difficulty_,
                    *session.ctx_.sim_events,
                    destination);
            };

        Log("headless_server_tick_interval_ms {}\n", frame_interval_ms);

        while (!g_shutdown_requested.load(std::memory_order_acquire))
        {
            const std::uint32_t desired_interval =
                og::server::compute_headless_tick_interval_ms(
                    level_data.world().timer_wait);
            if (desired_interval != frame_interval_ms)
            {
                frame_interval_ms = desired_interval;
                Log("headless_server_tick_interval_ms {}\n",
                    frame_interval_ms);
            }

            const auto tick_start = std::chrono::steady_clock::now();
            game_server.step();
            const auto tick_deadline =
                tick_start +
                std::chrono::milliseconds(frame_interval_ms);
            std::this_thread::sleep_until(tick_deadline);
        }

        if (io_initialized)
            io_exit();
        return 0;
    }
    catch (const std::exception& ex)
    {
        LogError("headless_server_fatal {}\n", ex.what());
    }
    catch (...)
    {
        LogError("headless_server_fatal unknown_exception\n");
    }

    if (io_initialized)
        io_exit();
    return 1;
}
