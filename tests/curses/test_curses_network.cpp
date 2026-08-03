/* Tests for CursesNetwork (host/join lobby + networked sessions).
 *
 * Real WebSocket loopback in a single-process test is flaky, so the whole flow
 * is exercised over an InProcessTransport with no real sockets, exactly as the
 * production code allows (the lobby + sessions are transport-agnostic). One
 * in-process server hosts a LobbyServer (owned by the host lobby) plus two client
 * transports off it — one loopback for the host's own player-0 display, one for
 * the joiner (player 1). The handshake is driven to a start, then the host's
 * authoritative GameServer + co-located mirror GameClient and the joiner's mirror
 * GameClient are advanced together (server.step + both clients poll, swapping the
 * gameplay context per world like LocalCursesSession). Convergence is asserted by
 * comparing both mirror worlds' entity ids/positions against the authoritative
 * server world.
 *
 * These mirror the existing engine net tests (tests/test_network_fixture.h) but
 * route through the ncurses client's own session/lobby construction.
 */
#include <gtest/gtest.h>

#include <openglad/platform/curses/curses_network.h>
#include <openglad/platform/curses/curses_game_runtime.h>
#include <openglad/platform/curses/curses_input.h>
#include <openglad/platform/curses/curses_renderer.h>
#include <openglad/platform/curses/headless_terminal.h>
#include <openglad/platform/curses/clock.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/lobby_server.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/platform/net_transport_websocket_client.h>
#include <openglad/resources/company.h>
#include <openglad/resources/io_common.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef OPENGLAD_SERVER_TEST_EXECUTABLE
#define OPENGLAD_SERVER_TEST_EXECUTABLE "openglad_server"
#endif

using namespace og::curses;

// Test-only construction hooks exported from curses_network.cpp. They let a
// single-process test drive the full host+join flow over an injected
// InProcessTransport (no real sockets). Not part of the public header.
namespace og::curses {
std::unique_ptr<CursesLobby> make_host_lobby_over_transport_for_testing(
    SaveData& save, int difficulty,
    std::shared_ptr<og::sim::ITransport> combined_transport,
    std::shared_ptr<og::sim::InProcessTransport> host_client_transport);
std::unique_ptr<CursesLobby> make_join_lobby_over_transport_for_testing(
    SaveData& save, int difficulty,
    std::shared_ptr<og::sim::ITransport> transport,
    og::sim::PeerId server_peer_id);
int curses_network_testing_exercise_internal_helpers();
og::sim::LobbySaveDataEquivalent
curses_network_testing_build_join_save_equivalent(
    const og::sim::LobbyState& state);
int curses_network_testing_force_server_win(CursesGameSession& session,
                                            std::uint32_t pinned_team0_score);
int curses_network_testing_clear_server_team(CursesGameSession& session,
                                             short team);
} // namespace og::curses

namespace {

using namespace std::chrono_literals;

class ExternalServerProcess
{
public:
    explicit ExternalServerProcess(const std::vector<std::string>& arguments)
    {
        std::string directory_pattern =
            (std::filesystem::temp_directory_path() /
             "openglad-server-process-XXXXXX").string();
        std::vector<char> writable(directory_pattern.begin(),
                                   directory_pattern.end());
        writable.push_back('\0');
        if (char* created = ::mkdtemp(writable.data()))
            config_directory_ = created;
        if (config_directory_.empty())
            return;

        int output_pipe[2]{};
        if (::pipe(output_pipe) != 0)
            return;
        const int flags = ::fcntl(output_pipe[0], F_GETFL, 0);
        if (flags >= 0)
            (void)::fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
        (void)::fcntl(output_pipe[0], F_SETFD, FD_CLOEXEC);
        (void)::fcntl(output_pipe[1], F_SETFD, FD_CLOEXEC);

        const std::filesystem::path executable =
            OPENGLAD_SERVER_TEST_EXECUTABLE;
        std::vector<std::string> owned_argv;
        owned_argv.reserve(arguments.size() + 1);
        owned_argv.push_back(executable.string());
        owned_argv.insert(owned_argv.end(), arguments.begin(), arguments.end());
        std::vector<char*> child_argv;
        child_argv.reserve(owned_argv.size() + 1);
        for (std::string& argument : owned_argv)
            child_argv.push_back(argument.data());
        child_argv.push_back(nullptr);

        pid_ = ::fork();
        if (pid_ < 0) {
            pid_ = -1;
            (void)::close(output_pipe[0]);
            (void)::close(output_pipe[1]);
            return;
        }
        if (pid_ == 0) {
            (void)::close(output_pipe[0]);
            if (::dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
                ::dup2(output_pipe[1], STDERR_FILENO) < 0) {
                _exit(126);
            }
            if (output_pipe[1] > STDERR_FILENO)
                (void)::close(output_pipe[1]);

            (void)::setenv("OPENGLAD_CONFIG_DIR",
                           config_directory_.c_str(), 1);
            if (!executable.parent_path().empty() &&
                ::chdir(executable.parent_path().c_str()) != 0)
                _exit(126);
            ::execv(executable.c_str(), child_argv.data());
            _exit(127);
        }

        (void)::close(output_pipe[1]);
        output_fd_ = output_pipe[0];
    }

    ~ExternalServerProcess()
    {
        if (pid_ > 0) {
            (void)::kill(pid_, SIGKILL);
            while (::waitpid(pid_, nullptr, 0) < 0 && errno == EINTR) {
            }
        }
        if (output_fd_ >= 0)
            (void)::close(output_fd_);
        std::error_code ignored;
        if (!config_directory_.empty())
            std::filesystem::remove_all(config_directory_, ignored);
    }

    ExternalServerProcess(const ExternalServerProcess&) = delete;
    ExternalServerProcess& operator=(const ExternalServerProcess&) = delete;

    [[nodiscard]] bool launched() const { return pid_ > 0; }
    [[nodiscard]] const std::string& output() const { return output_; }

    bool wait_for_output(std::string_view expected,
                         std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            drain_output();
            if (output_.find(expected) != std::string::npos)
                return true;
            if (has_exited())
                break;
            pollfd readable{output_fd_, POLLIN, 0};
            (void)::poll(&readable, 1, 10);
        }
        drain_output();
        return output_.find(expected) != std::string::npos;
    }

    bool terminate_cleanly(std::chrono::milliseconds timeout = 10s)
    {
        if (pid_ <= 0)
            return exited_ && WIFEXITED(wait_status_) &&
                WEXITSTATUS(wait_status_) == 0;

        if (::kill(pid_, SIGTERM) != 0 && errno != ESRCH)
            return false;
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            drain_output();
            if (has_exited())
                break;
            std::this_thread::sleep_for(5ms);
        }
        drain_output();
        return exited_ && WIFEXITED(wait_status_) &&
            WEXITSTATUS(wait_status_) == 0;
    }

private:
    void drain_output()
    {
        if (output_fd_ < 0)
            return;
        for (;;) {
            char buffer[4096];
            const ssize_t count = ::read(output_fd_, buffer, sizeof(buffer));
            if (count > 0) {
                output_.append(buffer, static_cast<std::size_t>(count));
                continue;
            }
            if (count < 0 && errno == EINTR)
                continue;
            break;
        }
    }

    bool has_exited()
    {
        if (pid_ <= 0)
            return exited_;
        const pid_t waited = ::waitpid(pid_, &wait_status_, WNOHANG);
        if (waited == pid_) {
            pid_ = -1;
            exited_ = true;
        }
        return exited_;
    }

    std::filesystem::path config_directory_;
    pid_t pid_ = -1;
    int output_fd_ = -1;
    int wait_status_ = -1;
    bool exited_ = false;
    std::string output_;
};

std::optional<int> external_server_free_tcp_port()
{
    const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return std::nullopt;

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(0);
    if (::bind(fd, reinterpret_cast<const sockaddr*>(&address),
               sizeof(address)) != 0) {
        (void)::close(fd);
        return std::nullopt;
    }
    socklen_t length = sizeof(address);
    if (::getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
        (void)::close(fd);
        return std::nullopt;
    }
    const int port = ntohs(address.sin_port);
    (void)::close(fd);
    return port > 0 ? std::optional<int>(port) : std::nullopt;
}

template <typename Predicate>
bool poll_external_client_until(og::sim::WebSocketClientTransport& client,
                                Predicate&& predicate,
                                std::chrono::milliseconds timeout = 10s)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const std::vector<og::sim::TypedReceivedMessage> messages =
            client.poll_typed();
        if (predicate(messages))
            return true;
        std::this_thread::sleep_for(5ms);
    }
    return predicate(client.poll_typed());
}

void init_team_save(SaveData& save, short team, char family, const char* name)
{
    save.current_campaign = "org.openglad.gladiator";
    save.scen_num = 1;
    save.numplayers = 1;
    save.my_team = team;
    save.allied_mode = 0; // distinct teams so both players keep their own side
    og::ui::initialize_starting_team(save, {family});
    for (auto& member : save.team_list) {
        if (member) {
            member->teamnum = team;
            if (name)
                member->name = name;
        }
    }
}

og::sim::LobbyCharacterSlot make_network_roster_slot(
    std::uint8_t slot_index,
    std::int32_t guy_id,
    std::string name,
    std::int8_t family)
{
    return og::sim::LobbyCharacterSlot{
        .slot_index = slot_index,
        .character = og::sim::LobbyCharacterData{
            .guy_id = guy_id,
            .name = std::move(name),
            .family = family,
        },
    };
}

og::sim::LobbyMessage make_network_join(
    std::string name,
    short team,
    std::vector<og::sim::LobbyCharacterSlot> slots)
{
    og::sim::LobbyMessage message;
    message.payload = og::sim::LobbyJoinMessage{
        .player = og::sim::LobbyPlayer{
            .name = std::move(name),
            .company = {},
            .team = team,
            .character_slots = std::move(slots),
        },
    };
    return message;
}

// Snapshot of a world's living/object entities keyed by id, for convergence.
std::map<std::uint32_t, std::pair<int, int>> entity_positions(const GameWorld& w)
{
    std::map<std::uint32_t, std::pair<int, int>> out;
    for (const auto& up : w.oblist) {
        const walker* e = up.get();
        if (e == nullptr)
            continue;
        out[e->entity_id()] = {e->xpos(), e->ypos()};
    }
    return out;
}

// §4.3 ready gate: the host's start is denied until every non-host machine is
// ready. Ready the joiner and drive both sides until the host's roster reflects
// it, so a subsequent host GO is accepted.
void ready_curses_joiner(CursesLobby& host_lobby, CursesLobby& join_lobby,
                         HeadlessTerminal& host_term, HeadlessTerminal& join_term,
                         FakeClock& clock)
{
    (void)join_lobby.set_ready(true);
    for (int i = 0; i < 200; ++i) {
        host_lobby.poll(host_term, clock);
        join_lobby.poll(join_term, clock);
        for (const og::sim::LobbyPlayer& player : host_lobby.players()) {
            if (!player.is_host && player.ready)
                return;
        }
    }
}

// Run the lobby handshake to a start over the shared in-process transport, then
// hand back the two started sessions. The HeadlessTerminal/FakeClock feed
// poll(); the host presses 's' to start.
struct StartedGame {
    std::unique_ptr<CursesLobby> host_lobby;
    std::unique_ptr<CursesLobby> join_lobby;
    std::unique_ptr<CursesGameSession> host_session;
    std::unique_ptr<CursesGameSession> join_session;
};

StartedGame negotiate_and_start(SaveData& host_save, SaveData& join_save)
{
    // One in-process server hosts the LobbyServer; the host's own player and the
    // joiner each get a client transport off it.
    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    StartedGame game;
    game.host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, /*difficulty=*/1, server, host_client);
    // The joiner addresses the in-process server via its own local peer id (the
    // InProcessTransport routes a client's local_peer_id back to the server).
    game.join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, /*difficulty=*/1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;

    // Converge the roster: both peers must appear before we start.
    bool rostered = false;
    for (int i = 0; i < 200 && !rostered; ++i) {
        game.host_lobby->poll(host_term, clock);
        game.join_lobby->poll(join_term, clock);
        rostered = game.host_lobby->status_lines().size() > 0 &&
                   // 2 players present in the host's view
                   [&] {
                       int player_lines = 0;
                       for (const std::string& s : game.host_lobby->status_lines())
                           if (s.rfind("Players: ", 0) == 0)
                               return s == "Players: 2";
                       (void)player_lines;
                       return false;
                   }();
    }

    // §4.3 ready gate: the joiner must be ready before the host may start.
    ready_curses_joiner(*game.host_lobby, *game.join_lobby, host_term, join_term,
                        clock);

    // Host requests start, then both poll until the start is negotiated and the
    // sessions are ready.
    game.host_lobby->request_start();
    bool host_ready = false;
    bool join_ready = false;
    for (int i = 0; i < 200 && !(host_ready && join_ready); ++i) {
        host_ready = game.host_lobby->poll(host_term, clock) || host_ready;
        join_ready = game.join_lobby->poll(join_term, clock) || join_ready;
    }

    if (host_ready)
        game.host_session = game.host_lobby->take_session();
    if (join_ready)
        game.join_session = game.join_lobby->take_session();
    return game;
}

// Advance the host server + both mirror clients in lockstep. The host session
// owns the authoritative GameServer (its advance() steps the server then polls
// the host's mirror); the join session only polls its client. Inputs are sent
// each frame so the server has something to tick.
void advance_all(CursesGameSession& host, CursesGameSession& join, int frames)
{
    for (int i = 0; i < frames; ++i) {
        InputState idle;
        host.send_input(idle);
        join.send_input(idle);
        host.advance(); // server.step() + host mirror poll
        join.advance(); // join mirror poll
    }
}

bool status_contains(const CursesLobby& lobby, std::string_view needle)
{
    for (const std::string& line : lobby.status_lines()) {
        if (line.find(needle) != std::string::npos)
            return true;
    }
    return false;
}

} // namespace

TEST(CursesNetwork, host_lobby_builds_over_inprocess_transport)
{
    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();

    SaveData save;
    init_team_save(save, 0, FAMILY_SOLDIER, "Host");
    auto lobby = make_host_lobby_over_transport_for_testing(save, 1, server, host_client);
    ASSERT_NE(lobby, nullptr);
    EXPECT_TRUE(lobby->is_host());

    HeadlessTerminal term(24, 80);
    FakeClock clock;
    lobby->poll(term, clock);

    // The host registered itself; status reflects at least the host player.
    const std::vector<std::string> lines = lobby->status_lines();
    ASSERT_FALSE(lines.empty());
    EXPECT_GT(term.present_count(), 0) << "poll() renders the lobby";
}

TEST(CursesNetwork, internal_helpers_cover_message_and_session_paths)
{
    EXPECT_EQ(0,
              curses_network_testing_exercise_internal_helpers());
}

TEST(CursesNetwork, roster_reflects_two_players)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");
    // §2.5 curses parity: company + deploy counts ride the status lines.
    host_save.save_name = "HOST CURSES CO";
    join_save.save_name = "JOIN CURSES CO";
    join_save.team_list[0]->deployed = false;

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    auto host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, 1, server, host_client);
    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;

    bool two_players = false;
    for (int i = 0; i < 200 && !two_players; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
        bool host_sees_two = false;
        for (const std::string& s : host_lobby->status_lines()) {
            if (s == "Players: 2")
                host_sees_two = true;
        }
        // Both views must converge before the company/deploy pins below.
        two_players = host_sees_two &&
            status_contains(*join_lobby, "Players: 2");
    }
    EXPECT_TRUE(two_players) << "the host roster should list both players";

    // §2.5 curses parity (stage mp-columns): each player line names its
    // machine's company (the wire's LobbyPlayer::company) and per-seat
    // deploy counts; the lobby-wide Deployed line sums the wire flags —
    // the joiner's benched member drops it to 1/2.
    EXPECT_TRUE(status_contains(*host_lobby, "<HOST CURSES CO>"));
    EXPECT_TRUE(status_contains(*host_lobby, "<JOIN CURSES CO>"))
        << "the host sees the joiner's company off the wire";
    EXPECT_TRUE(status_contains(*host_lobby, "DEP 1/1"));
    EXPECT_TRUE(status_contains(*host_lobby, "DEP 0/1"))
        << "the joiner's benched seat shows an empty deploy count";
    EXPECT_TRUE(status_contains(*host_lobby, "Deployed: 1/2"));
    EXPECT_TRUE(status_contains(*join_lobby, "<HOST CURSES CO>"))
        << "the joiner sees the host's company off the wire";
    EXPECT_TRUE(status_contains(*join_lobby, "Deployed: 1/2"));
    EXPECT_TRUE(status_contains(*host_lobby, "(RED)"));
    EXPECT_TRUE(status_contains(*host_lobby, "(GREEN)"));
    EXPECT_FALSE(status_contains(*host_lobby, "(team 0)"))
        << "the roster must identify teams with their player-facing colors";

    // §9.12 (G5) census parity: the terminal lobby carries the same
    // session-status line as the SDL base camp header — role + machine/
    // player census for the host, host company for the joiner (the curses
    // lobby has no relay room code, so the room half stays empty).
    EXPECT_TRUE(status_contains(*host_lobby, "HOSTING 2 MACH / 2 PLYR"));
    EXPECT_TRUE(status_contains(*join_lobby,
                                "JOINED - HOST: HOST CURSES CO"));

    // The joiner should also observe the shared lobby (>=1 player visible).
    bool join_sees_lobby = false;
    for (const std::string& s : join_lobby->status_lines()) {
        if (s.rfind("Players: ", 0) == 0)
            join_sees_lobby = true;
    }
    EXPECT_TRUE(join_sees_lobby) << "the joiner should observe the lobby roster";
}

// A joining terminal client must not claim readiness while a class-pack
// transfer is still in flight — the start gate would otherwise open on a
// machine missing the very Lua the deterministic sim runs (the same contract
// PickerNetworkClient.join_ready_is_refused_while_a_pack_transfer_is_pending
// pins for the SDL client). The server announces a pack this machine does
// not have and never serves a chunk, so the joiner requests it, surfaces the
// transfer's progress through its status log, and stays mid-transfer.
TEST(CursesNetwork, join_ready_refused_while_pack_transfer_pending)
{
    SaveData join_save;
    init_team_save(join_save, 1, FAMILY_ELF, "PackJoiner");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto join_client = server->create_client_transport();

    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());
    ASSERT_NE(nullptr, join_lobby);

    // Announce a pack this machine does not have, then never serve a single
    // chunk: the client requests it and stays mid-transfer.
    auto manifest = std::make_shared<og::sim::PackManifestMessage>();
    manifest->pack_index = 0;
    manifest->pack_count = 1;
    manifest->pack_id = "org.test.neverserved";
    manifest->version = "1";
    manifest->files.push_back(
        og::sim::PackManifestFileEntry{.path = "scripts/ghost.lua",
                                       .size_bytes = 64u,
                                       .hash64 = 0x0123456789abcdefull});
    server->send_pack_manifest(join_client->local_peer_id(), manifest);

    // One poll pumps the manifest into the transfer client (the in-process
    // pair delivers synchronously), which logs "Receiving pack ... (0%)".
    HeadlessTerminal term(24, 80);
    FakeClock clock;
    join_lobby->poll(term, clock);

    EXPECT_FALSE(join_lobby->set_ready(true))
        << "readiness must not be claimable while a pack is still arriving";
    EXPECT_FALSE(join_lobby->local_ready());
    EXPECT_TRUE(status_contains(*join_lobby, "Waiting for pack transfer"))
        << "the refused ready keeps the transfer status on screen";
}

TEST(CursesNetwork,
     join_game_start_assembly_matches_host_with_private_slot_and_id_collisions)
{
    auto transport = og::sim::InProcessTransport::create_server();
    transport->accept_connections();
    auto host_client = transport->create_client_transport();
    auto join_client = transport->create_client_transport();
    og::sim::LobbyServer authority(*transport);

    const auto send_join =
        [](const std::shared_ptr<og::sim::InProcessTransport>& client,
           og::sim::LobbyMessage message) {
            client->send_lobby_message(
                client->local_peer_id(),
                std::make_shared<og::sim::LobbyMessage>(std::move(message)));
        };

    // Both private companies use slots 0 and 3, and both independently gave
    // their first member guy id 6. The joiner's second id is invalid too.
    // Authority and curses joiner must derive one identical, dense match
    // roster while retaining the original owner/save coordinates.
    send_join(
        host_client,
        make_network_join(
            "Host",
            0,
            {make_network_roster_slot(
                 0, 6, "Host Zero", FAMILY_SOLDIER),
             make_network_roster_slot(
                 3, 1, "Host Three", FAMILY_ARCHER)}));
    send_join(
        join_client,
        make_network_join(
            "Joiner",
            1,
            {make_network_roster_slot(
                 0, 6, "Join Zero", FAMILY_MAGE),
             make_network_roster_slot(
                 3, -1, "Join Three", FAMILY_CLERIC)}));
    authority.poll_incoming_messages();

    ASSERT_EQ(2u, authority.state().players.size());
    const og::sim::LobbySaveDataEquivalent host_equivalent =
        authority.build_save_data_equivalent();
    const og::sim::LobbySaveDataEquivalent join_equivalent =
        curses_network_testing_build_join_save_equivalent(authority.state());
    EXPECT_EQ(host_equivalent, join_equivalent)
        << "host authority and curses joiner must seed the same world";

    ASSERT_EQ(4u, join_equivalent.team_list.size());
    const std::vector<std::string> expected_names{
        "Host Zero", "Join Zero", "Host Three", "Join Three"};
    const std::vector<std::int32_t> expected_ids{6, 0, 1, 2};
    const std::vector<std::uint8_t> expected_owners{0, 1, 0, 1};
    const std::vector<std::uint8_t> expected_private_slots{0, 0, 3, 3};
    for (std::size_t index = 0; index < join_equivalent.team_list.size();
         ++index)
    {
        const og::sim::LobbyCharacterSlot& slot =
            join_equivalent.team_list[index];
        EXPECT_EQ(index, slot.slot_index);
        EXPECT_EQ(expected_names[index], slot.character.name);
        EXPECT_EQ(expected_ids[index], slot.character.guy_id);
        EXPECT_EQ(expected_owners[index], slot.owner_player_index);
        EXPECT_EQ(expected_private_slots[index], slot.owner_save_slot);
    }
}

// §2.7 curses parity (stage ready-go-slot): the lobby is the curses MP
// surface — every peer's status lines carry the shared cross-control label,
// the host's 'c' key toggles it (a SETTINGS change the server answers by
// clearing every non-host machine's ready, §4.5), and a joiner's 'c' only
// surfaces the host-controls guard.
TEST(CursesNetwork, lobby_cross_control_key_host_toggles_and_clears_ready)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");
    host_save.cross_control = 0;

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    auto host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, 1, server, host_client);
    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;

    for (int i = 0; i < 200; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }
    EXPECT_TRUE(status_contains(*host_lobby, "Control: CTRL: OWN"));
    EXPECT_TRUE(status_contains(*join_lobby, "Control: CTRL: OWN"))
        << "every peer sees the mode that changes its own rights (§2.7)";

    ready_curses_joiner(*host_lobby, *join_lobby, host_term, join_term, clock);
    EXPECT_TRUE(status_contains(*host_lobby, "[ready]"));

    // Non-host 'c': guard message only, nothing toggles, ready survives.
    join_term.push_char(U'c');
    for (int i = 0; i < 50; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }
    EXPECT_TRUE(status_contains(*join_lobby, "Host controls cross-control"));
    EXPECT_TRUE(status_contains(*join_lobby, "Control: CTRL: OWN"));
    EXPECT_TRUE(status_contains(*host_lobby, "[ready]"))
        << "a denied joiner toggle must not clear ready";

    // Host 'c': both peers converge on CTRL: ALL and the settings change
    // clears the joiner's ready (§4.5).
    host_term.push_char(U'c');
    bool converged = false;
    for (int i = 0; i < 200 && !converged; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
        converged = status_contains(*host_lobby, "Control: CTRL: ALL") &&
            status_contains(*join_lobby, "Control: CTRL: ALL") &&
            !status_contains(*host_lobby, "[ready]");
    }
    EXPECT_TRUE(status_contains(*host_lobby, "Control: CTRL: ALL"));
    EXPECT_TRUE(status_contains(*join_lobby, "Control: CTRL: ALL"))
        << "the toggle must replicate to the joiner's status";
    EXPECT_FALSE(status_contains(*host_lobby, "[ready]"))
        << "a settings change clears every non-host machine's ready (§4.5)";

    // Toggle back: sanitized {0,1} round trip.
    host_term.push_char(U'C');
    bool reverted = false;
    for (int i = 0; i < 200 && !reverted; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
        reverted = status_contains(*join_lobby, "Control: CTRL: OWN");
    }
    EXPECT_TRUE(status_contains(*host_lobby, "Control: CTRL: OWN"));
    EXPECT_TRUE(reverted);
}

// §2.6 curses parity (the lobby is the curses MP surface): the 'r' key runs
// the client ready gate the SDL twin runs — a joiner with brought
// characters, none deployed, and cross-control OFF is denied with the
// DEPLOY AT LEAST ONE caption and never readies; once a character is
// deployed, 'r' readies and the flag replicates to the host.
TEST(CursesNetwork, lobby_ready_key_gates_on_zero_deployed)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");
    ASSERT_TRUE(join_save.team_list[0] != nullptr);
    join_save.team_list[0]->deployed = false;  // brought but benched
    host_save.cross_control = 0;

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    auto host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, 1, server, host_client);
    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;

    for (int i = 0; i < 200; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }

    // Benched roster + cross-control OFF: 'r' is denied with the §2.6
    // caption; nothing is sent, so the host never sees [ready].
    join_term.push_char(U'r');
    for (int i = 0; i < 50; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }
    EXPECT_FALSE(join_lobby->local_ready())
        << "a gated 'r' must not set ready";
    EXPECT_TRUE(status_contains(*join_lobby, "DEPLOY AT LEAST ONE"))
        << "the denial must surface the §2.6 caption";
    EXPECT_FALSE(status_contains(*host_lobby, "[ready]"))
        << "a denied ready must not replicate";

    // Deploy the character: the gate opens and 'r' readies + replicates.
    join_save.team_list[0]->deployed = true;
    join_term.push_char(U'r');
    bool host_sees_ready = false;
    for (int i = 0; i < 200 && !host_sees_ready; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
        host_sees_ready = status_contains(*host_lobby, "[ready]");
    }
    EXPECT_TRUE(join_lobby->local_ready());
    EXPECT_TRUE(host_sees_ready)
        << "the accepted ready must replicate to the host";
}

TEST(CursesNetwork, cancel_tears_down_cleanly)
{
    SaveData save;
    init_team_save(save, 0, FAMILY_SOLDIER, "Host");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();

    auto lobby = make_host_lobby_over_transport_for_testing(save, 1, server, host_client);
    ASSERT_NE(lobby, nullptr);

    HeadlessTerminal term(24, 80);
    FakeClock clock;
    lobby->poll(term, clock);

    // Cancel should not crash, and a subsequent poll reports no start.
    lobby->cancel();
    EXPECT_FALSE(lobby->poll(term, clock))
        << "a cancelled lobby never negotiates a start";
    // No session can be taken after cancel.
    EXPECT_EQ(lobby->take_session(), nullptr);
}

TEST(CursesNetwork, esc_key_cancels_lobby)
{
    SaveData save;
    init_team_save(save, 0, FAMILY_SOLDIER, "Host");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();

    auto lobby = make_host_lobby_over_transport_for_testing(save, 1, server, host_client);
    ASSERT_NE(lobby, nullptr);

    HeadlessTerminal term(24, 80);
    FakeClock clock;
    term.push_special(KeyCode::Escape);
    EXPECT_FALSE(lobby->poll(term, clock)) << "Esc cancels and returns false";
}

TEST(CursesNetwork, run_curses_lobby_returns_default_result_when_cancelled)
{
    SaveData save;
    init_team_save(save, 0, FAMILY_SOLDIER, "Host");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();

    auto lobby = make_host_lobby_over_transport_for_testing(save, 1, server, host_client);
    ASSERT_NE(lobby, nullptr);

    HeadlessTerminal term(24, 80);
    FakeClock clock;
    term.push_char(U'q');
    const GameRunResult result = run_curses_lobby(*lobby, term, clock);
    EXPECT_FALSE(result.ended);
    EXPECT_FALSE(result.withdrew);
    EXPECT_FALSE(result.quit_app);
    EXPECT_TRUE(lobby->cancelled());
}

TEST(CursesNetwork, key_releases_do_not_start_or_cancel_lobby)
{
    SaveData save;
    init_team_save(save, 0, FAMILY_SOLDIER, "Host");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();

    auto lobby = make_host_lobby_over_transport_for_testing(save, 1, server, host_client);
    ASSERT_NE(lobby, nullptr);

    HeadlessTerminal term(24, 80);
    FakeClock clock;
    term.push_char_release(U's');
    term.push_char_release(U'q');
    term.push_special_release(KeyCode::Enter);
    EXPECT_FALSE(lobby->poll(term, clock));
    EXPECT_FALSE(lobby->cancelled());
    EXPECT_EQ(lobby->take_session(), nullptr);
}

TEST(CursesNetwork, joiner_start_request_is_noop)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    auto host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, 1, server, host_client);
    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());

    ASSERT_TRUE(host_lobby->is_host());
    ASSERT_FALSE(join_lobby->is_host());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;
    for (int i = 0; i < 100; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }

    join_lobby->request_start();
    bool host_started = false;
    bool join_started = false;
    for (int i = 0; i < 50; ++i) {
        host_started = host_lobby->poll(host_term, clock) || host_started;
        join_started = join_lobby->poll(join_term, clock) || join_started;
    }
    EXPECT_FALSE(host_started);
    EXPECT_FALSE(join_started);
    EXPECT_EQ(host_lobby->take_session(), nullptr);
    EXPECT_EQ(join_lobby->take_session(), nullptr);
}

TEST(CursesNetwork, host_start_denial_is_correlated_before_retry)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();
    auto host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, 1, server, host_client);
    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;
    for (int i = 0; i < 200; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }

    // Request 1 is denied because the joiner is not ready. The matching
    // request id releases the pending state and surfaces the real reason.
    host_lobby->request_start();
    for (int i = 0; i < 100; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }
    EXPECT_TRUE(status_contains(
        *host_lobby, "Waiting for other machines"));
    EXPECT_EQ(host_lobby->take_session(), nullptr);

    // Request 2 has a fresh id; after readiness it starts both peers.
    ready_curses_joiner(
        *host_lobby, *join_lobby, host_term, join_term, clock);
    host_lobby->request_start();
    bool host_started = false;
    bool join_started = false;
    for (int i = 0; i < 200 && !(host_started && join_started); ++i) {
        host_started = host_lobby->poll(host_term, clock) || host_started;
        join_started = join_lobby->poll(join_term, clock) || join_started;
    }
    EXPECT_TRUE(host_started);
    EXPECT_TRUE(join_started);
}

TEST(CursesNetwork, host_and_join_sessions_start_and_converge)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    StartedGame game = negotiate_and_start(host_save, join_save);
    ASSERT_NE(game.host_session, nullptr) << "host session should start";
    ASSERT_NE(game.join_session, nullptr) << "join session should start";

    // Both mirror worlds load the level grid.
    EXPECT_GT(game.host_session->mirror_world().grid.w, 0);
    EXPECT_GT(game.join_session->mirror_world().grid.w, 0);

    // Advance the authoritative server + both mirror clients in lockstep so the
    // initial keyframe + a few ticks propagate to both mirrors.
    advance_all(*game.host_session, *game.join_session, 30);

    const GameWorld& host_mirror = game.host_session->mirror_world();
    const GameWorld& join_mirror = game.join_session->mirror_world();

    ASSERT_GT(host_mirror.oblist.size(), 0u)
        << "host mirror should be populated by the keyframe";
    ASSERT_GT(join_mirror.oblist.size(), 0u)
        << "join mirror should be populated by the keyframe";

    // Both mirrors converge to the same set of entity ids and positions.
    const auto host_pos = entity_positions(host_mirror);
    const auto join_pos = entity_positions(join_mirror);
    EXPECT_EQ(host_pos.size(), join_pos.size())
        << "both mirrors should hold the same number of entities";

    int matched = 0;
    for (const auto& [id, pos] : host_pos) {
        const auto it = join_pos.find(id);
        ASSERT_NE(it, join_pos.end())
            << "entity id " << id << " present on host mirror, missing on join mirror";
        EXPECT_EQ(pos, it->second)
            << "entity id " << id << " diverged between mirrors";
        ++matched;
    }
    EXPECT_GT(matched, 0) << "at least one entity should be mirrored on both sides";

    EXPECT_GT(game.host_session->next_input_tick(), 0u);
    EXPECT_GT(game.join_session->next_input_tick(), 0u);
    game.join_session->request_abort();
    game.host_session->request_abort();
    for (int i = 0; i < 30 && !game.host_session->ended(); ++i)
        advance_all(*game.host_session, *game.join_session, 1);
    EXPECT_TRUE(game.host_session->ended())
        << "either peer's abort request ends the shared mission";
}

// §4.4 install + snapshot v9 consumption on the curses stack: the host
// session derives the control policy from the negotiated cross-control
// setting at install time (owner-locked iff cross-control is OFF) and
// stamps the machine map (host machine 0 deployed, joiner machine 1
// deployed); BOTH mirrors render the scalars straight from the wire.
TEST(CursesNetwork, install_derives_control_policy_and_mirrors_consume_v9)
{
    {
        SaveData host_save;
        SaveData join_save;
        init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
        init_team_save(join_save, 1, FAMILY_ELF, "Joiner");
        ASSERT_EQ(0, static_cast<int>(host_save.cross_control))
            << "cross-control defaults OFF: the install derives owner-locked";

        StartedGame game = negotiate_and_start(host_save, join_save);
        ASSERT_NE(game.host_session, nullptr);
        ASSERT_NE(game.join_session, nullptr);
        advance_all(*game.host_session, *game.join_session, 30);

        for (const GameWorld* mirror : {&game.host_session->mirror_world(),
                                        &game.join_session->mirror_world()})
        {
            EXPECT_EQ(og::sim::kControlPolicyOwnerLocked,
                      mirror->control_policy)
                << "cross-control OFF must reach the mirror as owner-locked";
            EXPECT_EQ(og::sim::encode_player_machine(0, true),
                      mirror->player_machine[0]);
            EXPECT_EQ(og::sim::encode_player_machine(1, true),
                      mirror->player_machine[1]);
            EXPECT_EQ(og::sim::kPlayerMachineNone, mirror->player_machine[2]);
        }
    }
    {
        // Cross-control ON twin: the identical staging derives the LEGACY
        // policy; the machine map is still stamped and replicated.
        SaveData host_save;
        SaveData join_save;
        init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
        init_team_save(join_save, 1, FAMILY_ELF, "Joiner");
        host_save.cross_control = 1;

        StartedGame game = negotiate_and_start(host_save, join_save);
        ASSERT_NE(game.host_session, nullptr);
        ASSERT_NE(game.join_session, nullptr);
        advance_all(*game.host_session, *game.join_session, 30);

        for (const GameWorld* mirror : {&game.host_session->mirror_world(),
                                        &game.join_session->mirror_world()})
        {
            EXPECT_EQ(og::sim::kControlPolicyLegacy, mirror->control_policy)
                << "cross-control ON must reach the mirror as legacy";
            EXPECT_EQ(og::sim::encode_player_machine(0, true),
                      mirror->player_machine[0]);
            EXPECT_EQ(og::sim::encode_player_machine(1, true),
                      mirror->player_machine[1]);
        }
    }
}

// §4.2 deploy filter over the curses stack: a benched host member never
// enters the level — the host's build_save_data_equivalent and the joiner's
// build_join_save_equivalent apply the SAME filter, so both mirrors converge
// on a world without the benched character.
TEST(CursesNetwork, benched_member_never_enters_the_level)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    // Second host member, BENCHED: replicates in the lobby, filtered from
    // the match assembly.
    host_save.team_list[1] = std::make_unique<guy>(FAMILY_MAGE);
    host_save.team_list[1]->name = "Reserve";
    host_save.team_list[1]->teamnum = 0;
    host_save.team_list[1]->deployed = false;
    host_save.team_size = 2;
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    StartedGame game = negotiate_and_start(host_save, join_save);
    ASSERT_NE(game.host_session, nullptr);
    ASSERT_NE(game.join_session, nullptr);

    advance_all(*game.host_session, *game.join_session, 30);

    const auto count_heroes = [](const GameWorld& world,
                                 int& reserve_count) {
        int heroes = 0;
        reserve_count = 0;
        for (const auto& up : world.oblist) {
            const walker* e = up.get();
            if (e == nullptr || e->dead() || e->myguy == nullptr)
                continue;
            ++heroes;
            if (e->myguy->name == "Reserve")
                ++reserve_count;
        }
        return heroes;
    };

    int host_reserve = 0;
    int join_reserve = 0;
    const int host_heroes =
        count_heroes(game.host_session->mirror_world(), host_reserve);
    const int join_heroes =
        count_heroes(game.join_session->mirror_world(), join_reserve);
    EXPECT_EQ(2, host_heroes)
        << "only the two deployed heroes spawn (host mirror)";
    EXPECT_EQ(2, join_heroes)
        << "only the two deployed heroes spawn (join mirror)";
    EXPECT_EQ(0, host_reserve) << "the benched member must not spawn";
    EXPECT_EQ(0, join_reserve) << "the benched member must not spawn";

    // The benched member is still in the host's save, flag intact.
    ASSERT_NE(nullptr, host_save.team_list[1]);
    EXPECT_FALSE(host_save.team_list[1]->deployed);
    EXPECT_EQ("Reserve", host_save.team_list[1]->name);
}

// §4.6/§4.8 "curses share persists": a WON networked curses session banks
// baseline + this machine's deploy-ratio SHARE into the active-company slot
// on disk, through the exact production call (run_level_loop ends the level
// and invokes commit_result_to_save -> persist_curses_networked_win). This
// fixture explicitly puts BOTH machines' heroes on money team 0 (owners 0 and
// 1, one deployed character each), so the pot splits 2 ways with the remainder going
// to the lowest player index (the host) — and the two shares CONSERVE to
// exactly the whole pot on the (process-shared) save0 file.
TEST(CursesNetwork, networked_win_persists_deploy_share_to_company_save)
{
    namespace fs = std::filesystem;
    ASSERT_EQ("save0", og::data::active_company_slot())
        << "the suite listener must have restored the default slot";

    // save0 hygiene (the test_curses_game_runtime precedent): preserve any
    // prior slot file and restore it on exit so this test cannot leak state
    // into later tests under --gtest_shuffle.
    const fs::path save0_path =
        fs::path(get_user_path()) / "save" / "save0.gtl";
    std::error_code ec;
    fs::create_directories(save0_path.parent_path(), ec);
    const bool had_save0 = fs::exists(save0_path, ec);
    if (had_save0)
        fs::copy_file(save0_path, save0_path.string() + ".netwinbak",
                      fs::copy_options::overwrite_existing, ec);
    struct RestoreGuard {
        fs::path save0_path;
        bool had_save0;
        ~RestoreGuard()
        {
            std::error_code ec2;
            if (had_save0) {
                fs::copy_file(save0_path.string() + ".netwinbak", save0_path,
                              fs::copy_options::overwrite_existing, ec2);
                fs::remove(save0_path.string() + ".netwinbak", ec2);
            } else {
                fs::remove(save0_path, ec2);
            }
        }
    } restore{save0_path, had_save0};

    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host Hero");
    init_team_save(join_save, 0, FAMILY_ELF, "Join Hero");
    // Together mode is a HOST setting (the lobby settings ride the host save).
    // It shares seat controls, but each fighter keeps its combat/team color.
    host_save.allied_mode = 1;
    // Distinct private save slots (host slot 0, joiner slot 1) so the two
    // machines' roster merges land side by side in the shared save0 file.
    join_save.team_list[1] = std::move(join_save.team_list[0]);

    StartedGame game = negotiate_and_start(host_save, join_save);
    ASSERT_NE(game.host_session, nullptr) << "allied host session should start";
    ASSERT_NE(game.join_session, nullptr) << "allied join session should start";
    advance_all(*game.host_session, *game.join_session, 30);

    // The company baseline this machine's shares are banked on top of. The
    // merge overlays each machine's own PRE-SESSION slots, so the disk roster
    // must (as in production) still hold the brought characters — seed stale
    // copies at the two private slots for the win merges to overwrite.
    SaveData base;
    base.reset();
    base.current_campaign = "org.openglad.gladiator";
    base.scen_num = 1;
    base.team_list[0] = std::make_unique<guy>(FAMILY_SOLDIER);
    base.team_list[0]->name = "Stale Host";
    base.team_list[0]->teamnum = 0;
    base.team_list[1] = std::make_unique<guy>(FAMILY_ELF);
    base.team_list[1]->name = "Stale Join";
    base.team_list[1]->teamnum = 1;
    base.team_size = 2;
    for (std::size_t team = 0; team < std::size(base.m_totalcash); ++team) {
        base.m_totalcash[team] = 5000u + 100u * static_cast<std::uint32_t>(team);
        base.m_totalscore[team] = 7000u + 100u * static_cast<std::uint32_t>(team);
    }
    ASSERT_TRUE(base.save("save0"));

    // A mid-level commit is a NO-OP (the genuine-win gate): nothing banks
    // before the level actually ends with a win.
    game.host_session->commit_result_to_save();
    {
        SaveData untouched;
        ASSERT_EQ(SaveDataIoError::None, untouched.load_with_error("save0"));
        EXPECT_EQ(base.m_totalcash[0], untouched.m_totalcash[0])
            << "an unfinished level must not bank any share";
        EXPECT_EQ(1, static_cast<int>(untouched.scen_num));
    }

    // Force the deterministic win on the authoritative world: pin the pot
    // (m_score[0] = 123 -> score delta 123, cash delta 246 + time bonus) and
    // clear exits + foes, then pump until BOTH machines report the end.
    const int slain =
        curses_network_testing_force_server_win(*game.host_session, 123u);
    ASSERT_GT(slain, 0) << "level 1 should contain foes to clear";
    bool both_ended = false;
    for (int round = 0; round < 400 && !both_ended; ++round) {
        advance_all(*game.host_session, *game.join_session, 5);
        both_ended =
            game.host_session->ended() && game.join_session->ended();
    }
    ASSERT_TRUE(both_ended) << "the forced win must end both sessions";
    EXPECT_EQ(0, game.host_session->ending()) << "a genuine win, not an abort";
    EXPECT_EQ(0, game.join_session->ending());
    EXPECT_EQ(2, game.host_session->next_level());

    // HOST commit through the production loop tail: run_level_loop sees the
    // ended session, breaks, and calls commit_result_to_save (the §4.6 curses
    // share persist).
    HeadlessTerminal term(24, 80);
    FakeClock clock;
    CursesInput input;
    CursesRenderer renderer;
    const GameRunResult host_result = run_level_loop(
        *game.host_session, term, clock, input, renderer,
        LevelLoopOptions{.max_frames = 20, .no_pacing = true, .render = false});
    ASSERT_TRUE(host_result.ended);
    ASSERT_EQ(0, host_result.ending);

    std::uint32_t host_cash_delta = 0;
    {
        SaveData after_host;
        ASSERT_EQ(SaveDataIoError::None, after_host.load_with_error("save0"));
        ASSERT_GE(after_host.m_totalcash[0], base.m_totalcash[0]);
        host_cash_delta = after_host.m_totalcash[0] - base.m_totalcash[0];
        // Two contributors, one each: score 123 splits 61/61 with the
        // remainder to the LOWEST player index — the host (player 0) banks 62.
        EXPECT_EQ(base.m_totalscore[0] + 62u, after_host.m_totalscore[0])
            << "host score share = floor(123/2) + remainder";
        for (std::size_t team = 1; team < std::size(after_host.m_totalcash);
             ++team) {
            EXPECT_EQ(base.m_totalcash[team], after_host.m_totalcash[team])
                << "teams without contributors stay at the disk baseline";
            EXPECT_EQ(base.m_totalscore[team], after_host.m_totalscore[team]);
        }
        EXPECT_EQ(2, static_cast<int>(after_host.scen_num))
            << "the machine's own campaign cursor advances";
        EXPECT_TRUE(after_host.is_level_completed(1))
            << "a deployed machine earns completion credit (§4.7)";
    }

    // JOINER commit through the same production loop tail: banks ITS share on
    // top of the (already host-credited) shared save0 file.
    const GameRunResult join_result = run_level_loop(
        *game.join_session, term, clock, input, renderer,
        LevelLoopOptions{.max_frames = 20, .no_pacing = true, .render = false});
    ASSERT_TRUE(join_result.ended);
    ASSERT_EQ(0, join_result.ending);

    {
        SaveData after_both;
        ASSERT_EQ(SaveDataIoError::None, after_both.load_with_error("save0"));
        const std::uint32_t total_cash_delta =
            after_both.m_totalcash[0] - base.m_totalcash[0];
        // §4.6 CONSERVATION: the two deploy-ratio shares sum to exactly the
        // whole pot — never the v7 duplicated-pot overlay (which would have
        // banked the full delta twice, or the session's absolute totals).
        EXPECT_EQ(base.m_totalscore[0] + 123u, after_both.m_totalscore[0])
            << "score shares conserve to the exact pinned pot";
        EXPECT_GE(total_cash_delta, 246u)
            << "cash pot = 2x score + first-win time bonus";
        EXPECT_EQ(total_cash_delta - total_cash_delta / 2u, host_cash_delta)
            << "host (lowest player index) banked the remainder half, the "
               "joiner the floor half — the split conserves ONLY if both "
               "machines sized the pot identically from the synced state";
        // Both machines merged their own hero at its own private slot.
        ASSERT_NE(nullptr, after_both.team_list[0]);
        EXPECT_EQ("Host Hero", after_both.team_list[0]->name);
        ASSERT_NE(nullptr, after_both.team_list[1]);
        EXPECT_EQ("Join Hero", after_both.team_list[1]->name);
        EXPECT_TRUE(after_both.is_level_completed(1));
        EXPECT_EQ(2, static_cast<int>(after_both.scen_num));
    }
}

TEST(CursesNetwork, both_players_follow_distinct_avatars)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    StartedGame game = negotiate_and_start(host_save, join_save);
    ASSERT_NE(game.host_session, nullptr);
    ASSERT_NE(game.join_session, nullptr);

    advance_all(*game.host_session, *game.join_session, 30);

    const std::uint32_t host_avatar = game.host_session->followed_entity_id();
    const std::uint32_t join_avatar = game.join_session->followed_entity_id();
    ASSERT_NE(host_avatar, 0u) << "host should follow its player avatar";
    ASSERT_NE(join_avatar, 0u) << "joiner should follow its player avatar";
    EXPECT_NE(host_avatar, join_avatar)
        << "the two players should control different avatars";

    // The avatars are present on both mirrors (server-authoritative).
    EXPECT_NE(game.join_session->mirror_world().find_by_id(host_avatar), nullptr);
    EXPECT_NE(game.host_session->mirror_world().find_by_id(join_avatar), nullptr);
}

// §4.5 curses follow parity: when the joiner's whole team dies while the
// host's stays alive, the joiner's seat goes null (the bound-team wipe
// suppression keeps the level running), the session auto-enters follow mode
// on the host's hero, the HUD renders "(following Host)", and the seat's
// SwitchChar binding cycles the watched target through the shared
// follow-target selectors. Camera-only: nothing here claims control.
TEST(CursesNetwork, joiner_follows_after_team_wipe_and_switch_char_cycles)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    StartedGame game = negotiate_and_start(host_save, join_save);
    ASSERT_NE(game.host_session, nullptr);
    ASSERT_NE(game.join_session, nullptr);
    advance_all(*game.host_session, *game.join_session, 30);

    // Before the wipe: the joiner follows its own avatar, no follow caption.
    EXPECT_FALSE(game.join_session->follow_engaged());
    const std::uint32_t own_avatar = game.join_session->followed_entity_id();
    ASSERT_NE(0u, own_avatar);

    // Wipe the joiner's ENTIRE team on the authoritative server (troops on
    // its team would otherwise be claimed by the shared-pool reacquire).
    ASSERT_GE(og::curses::curses_network_testing_clear_server_team(
                  *game.host_session, 1),
              1);

    bool engaged = false;
    for (int i = 0; i < 120 && !engaged; ++i) {
        advance_all(*game.host_session, *game.join_session, 1);
        engaged = game.join_session->follow_engaged();
    }
    ASSERT_TRUE(engaged) << "the joiner's null seat must auto-enter follow mode";
    EXPECT_FALSE(game.join_session->ended())
        << "the host's living team keeps the level running";
    EXPECT_FALSE(game.host_session->follow_engaged())
        << "the host still controls its own hero";

    // Default target: the lowest player index with a live controlled walker
    // — the host's hero, wearing its OWNER's replicated user tag.
    const std::uint32_t followed = game.join_session->followed_entity_id();
    ASSERT_NE(0u, followed);
    EXPECT_NE(own_avatar, followed);
    {
        const walker* w =
            game.join_session->mirror_world().find_by_id(followed);
        ASSERT_NE(nullptr, w);
        EXPECT_FALSE(w->dead());
        ASSERT_NE(nullptr, w->myguy);
        EXPECT_EQ("Host", w->myguy->name);
        EXPECT_NE(-1, static_cast<int>(w->user()));
    }

    // The HUD captions the watched target.
    HeadlessTerminal term(24, 60);
    CursesRenderer renderer;
    renderer.draw(term, game.join_session->mirror_world(),
                  game.join_session->followed_entity_id(),
                  game.join_session->follow_engaged());
    EXPECT_NE(term.text_row(0).find("(following Host)"), std::string::npos)
        << "row 0 must caption the watched target; got: " << term.text_row(0);

    // SwitchChar cycles the watched target (the any-living fallback reaches
    // the scenario troops once the host hero is the current target).
    InputState cycle;
    cycle.players[0].held[static_cast<int>(InputAction::SwitchChar)] = true;
    cycle.players[0].pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    game.join_session->send_input(cycle);
    game.host_session->send_input(InputState{});
    game.host_session->advance();
    game.join_session->advance();

    const std::uint32_t cycled = game.join_session->followed_entity_id();
    ASSERT_NE(0u, cycled);
    EXPECT_NE(followed, cycled) << "SwitchChar must move the watched target";
    EXPECT_TRUE(game.join_session->follow_engaged());
    {
        const walker* w = game.join_session->mirror_world().find_by_id(cycled);
        ASSERT_NE(nullptr, w);
        EXPECT_FALSE(w->dead());
    }
}

TEST(CursesNetwork, host_input_propagates_to_joiner_mirror)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    StartedGame game = negotiate_and_start(host_save, join_save);
    ASSERT_NE(game.host_session, nullptr);
    ASSERT_NE(game.join_session, nullptr);

    // Settle the initial keyframe.
    advance_all(*game.host_session, *game.join_session, 10);

    const std::uint32_t host_avatar = game.host_session->followed_entity_id();
    ASSERT_NE(host_avatar, 0u);

    auto joiner_view_of_host = [&]() -> std::pair<int, int> {
        const walker* w = game.join_session->mirror_world().find_by_id(host_avatar);
        return w ? std::pair<int, int>{w->xpos(), w->ypos()}
                 : std::pair<int, int>{-1, -1};
    };
    const std::pair<int, int> before = joiner_view_of_host();
    ASSERT_NE(before.first, -1) << "the joiner must see the host's avatar";

    // The host drives its avatar; try several directions until the joiner's
    // mirror reflects movement (open ground exists around the spawn).
    const InputAction dirs[] = {InputAction::MoveRight, InputAction::MoveDown,
                                InputAction::MoveLeft, InputAction::MoveUp};
    bool moved = false;
    for (InputAction dir : dirs) {
        const std::pair<int, int> dir_start = joiner_view_of_host();
        for (int i = 0; i < 40 && !moved; ++i) {
            InputState host_in;
            host_in.players[0].held[static_cast<int>(dir)] = true;
            host_in.players[0].pressed[static_cast<int>(dir)] = (i == 0);
            InputState idle;
            game.host_session->send_input(host_in);
            game.join_session->send_input(idle);
            game.host_session->advance();
            game.join_session->advance();
            if (joiner_view_of_host() != dir_start)
                moved = true;
        }
        if (moved)
            break;
    }
    EXPECT_TRUE(moved)
        << "the host's movement should propagate to the joiner's mirror world";
}

TEST(CursesNetwork, take_session_is_idempotent)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    StartedGame game = negotiate_and_start(host_save, join_save);
    ASSERT_NE(game.host_session, nullptr);

    // The session was already taken inside negotiate_and_start; a second take on
    // the same lobby must not rebuild a duplicate session over the live transport.
    EXPECT_EQ(game.host_lobby->take_session(), nullptr)
        << "a session can only be taken once";
    EXPECT_EQ(game.join_lobby->take_session(), nullptr);
}

TEST(CursesNetwork, host_start_via_s_key)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    auto host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, 1, server, host_client);
    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;

    // Converge the roster.
    for (int i = 0; i < 200; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }

    // §4.3: the joiner readies so the host's GO is accepted.
    ready_curses_joiner(*host_lobby, *join_lobby, host_term, join_term, clock);

    // Pressing 's' on the host terminal requests the start (no direct call).
    host_term.push_char(U's');
    bool host_started = false;
    bool join_started = false;
    for (int i = 0; i < 200 && !(host_started && join_started); ++i) {
        host_started = host_lobby->poll(host_term, clock) || host_started;
        join_started = join_lobby->poll(join_term, clock) || join_started;
    }
    EXPECT_TRUE(host_started) << "the 's' key should start the game on the host";
    EXPECT_TRUE(join_started) << "the joiner should observe the start";

    // The host status shows itself flagged as the local player.
    bool saw_you_marker = false;
    for (const std::string& s : host_lobby->status_lines()) {
        if (s.find("[you]") != std::string::npos)
            saw_you_marker = true;
    }
    EXPECT_TRUE(saw_you_marker) << "the host roster should flag the local player";
}

TEST(CursesNetwork, host_start_via_uppercase_s_and_enter)
{
    for (const Key key : {Key::character(U'S'), Key::special(KeyCode::Enter)}) {
        SaveData host_save;
        SaveData join_save;
        init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
        init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

        auto server = og::sim::InProcessTransport::create_server();
        server->accept_connections();
        auto host_client = server->create_client_transport();
        auto join_client = server->create_client_transport();

        auto host_lobby = make_host_lobby_over_transport_for_testing(
            host_save, 1, server, host_client);
        auto join_lobby = make_join_lobby_over_transport_for_testing(
            join_save, 1, join_client, join_client->local_peer_id());

        HeadlessTerminal host_term(24, 80);
        HeadlessTerminal join_term(24, 80);
        FakeClock clock;

        for (int i = 0; i < 200; ++i) {
            host_lobby->poll(host_term, clock);
            join_lobby->poll(join_term, clock);
        }

        // §4.3: the joiner readies so the host's GO is accepted.
        ready_curses_joiner(*host_lobby, *join_lobby, host_term, join_term, clock);

        host_term.push_key(key);
        bool host_started = false;
        bool join_started = false;
        for (int i = 0; i < 200 && !(host_started && join_started); ++i) {
            host_started = host_lobby->poll(host_term, clock) || host_started;
            join_started = join_lobby->poll(join_term, clock) || join_started;
        }
        EXPECT_TRUE(host_started);
        EXPECT_TRUE(join_started);
    }
}

TEST(CursesNetwork, host_lobby_status_uses_default_campaign_and_team_fallback)
{
    SaveData save;
    init_team_save(save, 2, FAMILY_ELF, "Fallback");
    save.current_campaign.clear();
    save.my_team = MAX_PLAYERS;

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();

    auto lobby = make_host_lobby_over_transport_for_testing(save, 1, server, host_client);
    ASSERT_NE(lobby, nullptr);

    HeadlessTerminal term(24, 80);
    FakeClock clock;
    lobby->poll(term, clock);

    // The empty campaign id falls back to the default campaign, shown by its
    // human title (io_init installed the package, so the lookup succeeds).
    EXPECT_TRUE(status_contains(*lobby, "Campaign: Gladiator"));
    EXPECT_TRUE(status_contains(*lobby, "(BLUE)"));
    EXPECT_TRUE(status_contains(*lobby, "[host]"));
}

TEST(CursesNetwork, lobby_team_key_cycles_the_selected_owned_seat)
{
    SaveData save;
    init_team_save(save, 0, FAMILY_SOLDIER, "Keyboard");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto lobby = make_host_lobby_over_transport_for_testing(
        save, 1, server, host_client);
    ASSERT_NE(lobby, nullptr);

    HeadlessTerminal term(24, 90);
    FakeClock clock;
    lobby->poll(term, clock);
    const std::vector<std::uint8_t> local = lobby->local_player_indices();
    ASSERT_EQ(1u, local.size());

    term.push_char(U't');
    lobby->poll(term, clock);
    const auto players = lobby->players();
    const auto mine = std::find_if(
        players.begin(), players.end(),
        [&local](const og::sim::LobbyPlayer& player) {
            return player.player_index == local.front();
        });
    ASSERT_NE(mine, players.end());
    EXPECT_EQ(1, mine->team);
    EXPECT_NE(term.text_row(term.rows() - 1).find("[</>] seat"),
              std::string::npos);

    term.push_char(U'<');
    lobby->poll(term, clock);
    EXPECT_TRUE(status_contains(*lobby, "Selected P1"));
    term.push_special(KeyCode::Right);
    lobby->poll(term, clock);
    EXPECT_TRUE(status_contains(*lobby, "Selected P1"));
}

TEST(CursesNetwork, lobby_team_key_wraps_in_explicit_two_team_versus_domain)
{
    SaveData save;
    init_team_save(save, 0, FAMILY_SOLDIER, "CTF Keyboard");
    // The shared-teams rule rides the wire since protocol v12, derived from
    // the campaign's matchup: versus yaml key — the shipped modes campaign
    // is the versus campaign.
    save.current_campaign = "org.openglad.modes";
    save.ctf_team_count = 2;

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto lobby = make_host_lobby_over_transport_for_testing(
        save, 1, server, host_client);
    ASSERT_NE(lobby, nullptr);

    HeadlessTerminal term(24, 90);
    FakeClock clock;
    lobby->poll(term, clock);
    const std::vector<std::uint8_t> local = lobby->local_player_indices();
    ASSERT_EQ(1u, local.size());

    const auto local_team = [&]() -> short {
        for (const og::sim::LobbyPlayer& player : lobby->players()) {
            if (player.player_index == local.front())
                return player.team;
        }
        return -1;
    };

    term.push_char(U't');
    lobby->poll(term, clock);
    EXPECT_EQ(1, local_team());
    term.push_char(U't');
    lobby->poll(term, clock);
    EXPECT_EQ(0, local_team())
        << "explicit two-team versus must skip teams 2 and 3";
}

// The lobby "Level:" line reads scenario titles off the LOCAL mount, so when
// the mounted campaign differs from the lobby's campaign it must show the
// bare number — never the local campaign's title for a colliding level
// number (every campaign has a level 1).
TEST(CursesNetwork, lobby_level_title_requires_matching_mount)
{
    SaveData save;
    init_team_save(save, 2, FAMILY_ELF, "Mismatch");
    save.current_campaign = "org.openglad.modes"; // not the mounted campaign
    save.scen_num = 1; // collides with gladiator's scen1

    ASSERT_EQ(CampaignPackageIoError::None,
              mount_campaign_package_with_error("org.openglad.gladiator"));

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();

    auto lobby = make_host_lobby_over_transport_for_testing(save, 1, server, host_client);
    ASSERT_NE(lobby, nullptr);

    HeadlessTerminal term(24, 80);
    FakeClock clock;
    lobby->poll(term, clock);

    EXPECT_TRUE(status_contains(*lobby, "Campaign: Multiplayer Game Modes"));
    EXPECT_TRUE(status_contains(*lobby, "Level: 1"));
    EXPECT_FALSE(status_contains(*lobby, "Level: 1."))
        << "a mismatched mount must not serve another campaign's scen1 title";
}

TEST(CursesNetwork, malformed_join_url_reports_error)
{
    SaveData save;
    init_team_save(save, 0, FAMILY_SOLDIER, "Joiner");

    JoinOptions options;
    options.url = "";
    std::string error;
    std::unique_ptr<CursesLobby> lobby = make_join_lobby(save, options, &error);
    EXPECT_EQ(lobby, nullptr);
    EXPECT_FALSE(error.empty());
}

// Classic lobbies allow cooperative seat assignments: a joiner can choose the
// host's team and the authoritative roster replicates that shared assignment.
TEST(CursesNetwork, classic_lobby_allows_shared_team_request)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    auto host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, 1, server, host_client);
    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;

    bool two_players = false;
    for (int i = 0; i < 200 && !two_players; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
        two_players = host_lobby->players().size() == 2;
    }
    ASSERT_TRUE(two_players);

    (void)join_lobby->request_team_change(0);
    for (int i = 0; i < 50; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }

    const auto players = host_lobby->players();
    ASSERT_EQ(2u, players.size());
    short host_team = -1;
    short join_team = -1;
    for (const og::sim::LobbyPlayer& player : players) {
        if (player.is_host)
            host_team = static_cast<short>(player.team);
        else
            join_team = static_cast<short>(player.team);
    }
    EXPECT_EQ(0, host_team);
    EXPECT_EQ(0, join_team) << "classic lobbies allow cooperative seats";

    host_lobby->cancel();
    join_lobby->cancel();
}

// The terminal lobby exposes the same exact-seat operation as SDL Base Camp:
// a client may target its current global P#, but a foreign P# is rejected
// locally and never turns into a first-seat fallback on the server.
TEST(CursesNetwork, exact_seat_team_request_rejects_foreign_player)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    auto host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, 1, server, host_client);
    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;
    for (int i = 0; i < 200 && host_lobby->players().size() != 2; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }
    ASSERT_EQ(2u, host_lobby->players().size());

    const std::vector<std::uint8_t> join_indices =
        join_lobby->local_player_indices();
    ASSERT_EQ(1u, join_indices.size());
    const std::uint8_t join_index = join_indices.front();
    const auto initial_players = host_lobby->players();
    const auto host_it = std::find_if(
        initial_players.begin(), initial_players.end(),
        [join_index](const og::sim::LobbyPlayer& player) {
            return player.player_index != join_index;
        });
    ASSERT_NE(host_it, initial_players.end());

    EXPECT_FALSE(join_lobby->request_seat_team_change(
        host_it->player_index, 2));
    for (int i = 0; i < 20; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }
    const auto after_foreign = host_lobby->players();
    ASSERT_EQ(initial_players.size(), after_foreign.size());
    for (std::size_t i = 0; i < initial_players.size(); ++i)
        EXPECT_EQ(initial_players[i].team, after_foreign[i].team);

    // A joiner's send is asynchronous, so the immediate bool may be false;
    // the authoritative state after both sides poll must move only its seat.
    (void)join_lobby->request_seat_team_change(join_index, 2);
    for (int i = 0; i < 200; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
    }
    for (const og::sim::LobbyPlayer& player : host_lobby->players()) {
        if (player.player_index == join_index)
            EXPECT_EQ(2, player.team);
        else
            EXPECT_EQ(host_it->team, player.team);
    }

    host_lobby->cancel();
    join_lobby->cancel();
}

// P# is global, not capped at the four per-machine input slots. Five terminal
// clients prove that exact targeting still reaches P5 and leaves P1-P4 alone.
TEST(CursesNetwork, five_clients_can_change_global_player_five_exactly)
{
    constexpr int kClientCount = 5;
    std::vector<std::unique_ptr<SaveData>> saves;
    std::vector<std::shared_ptr<og::sim::InProcessTransport>> clients;
    std::vector<std::unique_ptr<CursesLobby>> lobbies;
    std::vector<std::unique_ptr<HeadlessTerminal>> terminals;
    saves.reserve(kClientCount);
    clients.reserve(kClientCount);
    lobbies.reserve(kClientCount);
    terminals.reserve(kClientCount);

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    for (int i = 0; i < kClientCount; ++i) {
        auto save = std::make_unique<SaveData>();
        const std::string name = "Client" + std::to_string(i + 1);
        init_team_save(
            *save, static_cast<short>(i % MAX_PLAYERS),
            static_cast<char>(FAMILY_SOLDIER + (i % 2)), name.c_str());
        saves.push_back(std::move(save));
        clients.push_back(server->create_client_transport());
        terminals.push_back(std::make_unique<HeadlessTerminal>(24, 80));
    }

    lobbies.push_back(make_host_lobby_over_transport_for_testing(
        *saves[0], 1, server, clients[0]));
    for (int i = 1; i < kClientCount; ++i) {
        lobbies.push_back(make_join_lobby_over_transport_for_testing(
            *saves[static_cast<std::size_t>(i)], 1,
            clients[static_cast<std::size_t>(i)],
            clients[static_cast<std::size_t>(i)]->local_peer_id()));
    }

    FakeClock clock;
    for (int attempt = 0;
         attempt < 500 &&
             lobbies.front()->players().size() != kClientCount;
         ++attempt)
    {
        for (int i = 0; i < kClientCount; ++i) {
            lobbies[static_cast<std::size_t>(i)]->poll(
                *terminals[static_cast<std::size_t>(i)], clock);
        }
    }
    ASSERT_EQ(kClientCount, lobbies.front()->players().size());

    const std::vector<std::uint8_t> fifth_indices =
        lobbies.back()->local_player_indices();
    ASSERT_EQ(1u, fifth_indices.size());
    ASSERT_GT(fifth_indices.front(), 3u);

    const auto before = lobbies.front()->players();
    const auto target_before = std::find_if(
        before.begin(), before.end(),
        [&fifth_indices](const og::sim::LobbyPlayer& player) {
            return player.player_index == fifth_indices.front();
        });
    ASSERT_NE(target_before, before.end());
    const short target_team =
        static_cast<short>((target_before->team + 1) % MAX_PLAYERS);

    (void)lobbies.back()->request_seat_team_change(
        fifth_indices.front(), target_team);
    for (int attempt = 0; attempt < 300; ++attempt) {
        for (int i = 0; i < kClientCount; ++i) {
            lobbies[static_cast<std::size_t>(i)]->poll(
                *terminals[static_cast<std::size_t>(i)], clock);
        }
    }

    const auto after = lobbies.front()->players();
    ASSERT_EQ(before.size(), after.size());
    for (const og::sim::LobbyPlayer& old_player : before) {
        const auto current = std::find_if(
            after.begin(), after.end(),
            [&old_player](const og::sim::LobbyPlayer& player) {
                return player.seat_id == old_player.seat_id;
            });
        ASSERT_NE(current, after.end());
        EXPECT_EQ(
            old_player.player_index == fifth_indices.front()
                ? target_team
                : old_player.team,
            current->team);
    }

    for (auto& lobby : lobbies)
        lobby->cancel();
}

// CTF-campaign lobbies use the same shared-team rule; the ready flag
// round-trips and shows up as a [ready] tag in the status lines.
TEST(CursesNetwork, ctf_lobby_team_change_and_ready_round_trip)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");
    host_save.current_campaign = "org.openglad.ctf"; // shared-team lobby

    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    auto host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, 1, server, host_client);
    auto join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, 1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;

    bool two_players = false;
    for (int i = 0; i < 200 && !two_players; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
        two_players = host_lobby->players().size() == 2;
    }
    ASSERT_TRUE(two_players);

    // Joiner moves onto the host's team (asynchronous: poll both sides).
    (void)join_lobby->request_team_change(0);
    bool shared = false;
    for (int i = 0; i < 200 && !shared; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
        const auto players = host_lobby->players();
        shared = players.size() == 2 && players[0].team == 0 &&
            players[1].team == 0;
    }
    EXPECT_TRUE(shared) << "CTF lobbies allow two humans on one team";

    // The host's own request is synchronous over the loopback link.
    EXPECT_TRUE(host_lobby->request_team_change(1));

    // Ready round trip + the [ready] status tag.
    EXPECT_FALSE(host_lobby->local_ready());
    EXPECT_TRUE(host_lobby->set_ready(true));
    EXPECT_TRUE(host_lobby->local_ready());

    (void)join_lobby->set_ready(true);
    bool join_ready_seen = false;
    for (int i = 0; i < 200 && !join_ready_seen; ++i) {
        host_lobby->poll(host_term, clock);
        join_lobby->poll(join_term, clock);
        for (const og::sim::LobbyPlayer& player : host_lobby->players()) {
            if (!player.is_host && player.ready)
                join_ready_seen = true;
        }
    }
    EXPECT_TRUE(join_ready_seen) << "the joiner's ready flag should replicate";
    EXPECT_TRUE(status_contains(*host_lobby, "[ready]"));

    // Any exact team change clears every ready seat on that machine.
    EXPECT_TRUE(host_lobby->local_ready());
    EXPECT_TRUE(host_lobby->request_team_change(0));
    EXPECT_FALSE(host_lobby->local_ready());

    host_lobby->cancel();
    join_lobby->cancel();
}

TEST(CursesNetworkProcess, dedicated_server_transitions_from_lobby_to_gameplay)
{
    const std::optional<int> port = external_server_free_tcp_port();
    ASSERT_TRUE(port.has_value());

    ExternalServerProcess server({
        "--host", "127.0.0.1",
        "--port", std::to_string(*port),
        "--lobby-poll-ms", "0",
        "--fps", "60",
    });
    ASSERT_TRUE(server.launched());
    ASSERT_TRUE(server.wait_for_output("headless_server_listening", 10s))
        << server.output();

    og::sim::WebSocketClientTransport::Options client_options;
    client_options.remote_peer_id = 1u;
    client_options.automatic_reconnection = false;
    og::sim::WebSocketClientTransport client(
        std::format("ws://127.0.0.1:{}", *port), client_options);
    client.accept_connections();

    ASSERT_TRUE(poll_external_client_until(
        client,
        [&client](const auto&) {
            return client.connected_peers() ==
                std::vector<og::sim::PeerId>{1u};
        })) << server.output();

    og::sim::LobbyCharacterSlot slot = make_network_roster_slot(
        0u, 7001, "Process Soldier", FAMILY_SOLDIER);
    slot.deployed = true;
    slot.character.strength = 12;
    slot.character.dexterity = 11;
    slot.character.constitution = 13;
    slot.character.intelligence = 10;
    slot.character.armor = 5;
    slot.character.level = 1;
    slot.character.teamnum = 0;

    constexpr std::uint32_t join_request_id = 41u;
    auto join = std::make_shared<og::sim::LobbyMessage>();
    join->payload = og::sim::LobbyJoinMessage{
        .player = og::sim::LobbyPlayer{
            .name = "Process Host",
            .company = "Process Company",
            .team = 0,
            .character_slots = {slot},
        },
        .request_id = join_request_id,
    };
    client.send_lobby_message(1u, join);

    std::optional<og::sim::LobbyState> joined_state;
    ASSERT_TRUE(poll_external_client_until(
        client,
        [&joined_state](const auto& messages) {
            for (const og::sim::TypedReceivedMessage& message : messages) {
                if (message.kind !=
                        og::sim::TypedReceivedMessageKind::LobbyState ||
                    !message.lobby_state ||
                    message.lobby_state->last_join_request_id !=
                        join_request_id) {
                    continue;
                }
                joined_state = *message.lobby_state;
                return true;
            }
            return false;
        })) << server.output();
    ASSERT_TRUE(joined_state.has_value());
    ASSERT_TRUE(joined_state->local_peer_is_host);
    ASSERT_EQ(1u, joined_state->players.size());
    EXPECT_TRUE(joined_state->players.front().is_host);
    EXPECT_EQ("Process Host", joined_state->players.front().name);
    ASSERT_EQ(1u, joined_state->players.front().character_slots.size());
    EXPECT_TRUE(joined_state->players.front().character_slots.front().deployed);

    constexpr std::uint32_t start_request_id = 42u;
    auto start = std::make_shared<og::sim::LobbyMessage>();
    start->payload = og::sim::LobbyStartGameMessage{
        .player_index = 0u,
        .request_id = start_request_id,
    };
    client.send_lobby_message(1u, start);

    ASSERT_TRUE(server.wait_for_output("headless_server_tick_interval_ms", 20s))
        << server.output();

    bool saw_start_confirmation = false;
    bool saw_initial_setup = false;
    bool saw_initial_snapshot = false;
    std::shared_ptr<og::sim::InitialSetupMessage> initial_setup;
    ASSERT_TRUE(poll_external_client_until(
        client,
        [&](const auto& messages) {
            for (const og::sim::TypedReceivedMessage& message : messages) {
                if (message.kind ==
                        og::sim::TypedReceivedMessageKind::LobbyMessage &&
                    message.lobby_message &&
                    message.lobby_message->kind() ==
                        og::sim::LobbyMessageKind::StartGame) {
                    const auto& confirmation =
                        std::get<og::sim::LobbyStartGameMessage>(
                            message.lobby_message->payload);
                    if (confirmation.request_id == start_request_id)
                        saw_start_confirmation = true;
                }
                if (message.kind ==
                        og::sim::TypedReceivedMessageKind::InitialSetup &&
                    message.initial_setup) {
                    saw_initial_setup = true;
                    initial_setup = message.initial_setup;
                }
                if (message.kind ==
                        og::sim::TypedReceivedMessageKind::Snapshot &&
                    message.snapshot) {
                    saw_initial_snapshot = true;
                }
            }
            return saw_start_confirmation && saw_initial_setup &&
                saw_initial_snapshot;
        })) << server.output();

    ASSERT_TRUE(initial_setup);
    EXPECT_EQ(1, initial_setup->current_scenario);
    EXPECT_FALSE(initial_setup->level_title.empty());
    EXPECT_FALSE(initial_setup->guys.empty());

    EXPECT_TRUE(server.terminate_cleanly()) << server.output();
    EXPECT_NE(std::string::npos,
              server.output().find("headless_server_listening"));
    EXPECT_NE(std::string::npos,
              server.output().find("headless_server_tick_interval_ms"));
}
