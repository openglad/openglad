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
#include <openglad/platform/curses/headless_terminal.h>
#include <openglad/platform/curses/clock.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
} // namespace og::curses

namespace {

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
    constexpr int kExpectedInternalHelperChecks = 11;
    EXPECT_EQ(kExpectedInternalHelperChecks,
              curses_network_testing_exercise_internal_helpers());
}

TEST(CursesNetwork, roster_reflects_two_players)
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
        for (const std::string& s : host_lobby->status_lines()) {
            if (s == "Players: 2")
                two_players = true;
        }
    }
    EXPECT_TRUE(two_players) << "the host roster should list both players";

    // The joiner should also observe the shared lobby (>=1 player visible).
    bool join_sees_lobby = false;
    for (const std::string& s : join_lobby->status_lines()) {
        if (s.rfind("Players: ", 0) == 0)
            join_sees_lobby = true;
    }
    EXPECT_TRUE(join_sees_lobby) << "the joiner should observe the lobby roster";
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

    EXPECT_TRUE(status_contains(*lobby, "Campaign: org.openglad.gladiator"));
    EXPECT_TRUE(status_contains(*lobby, "team 2"));
    EXPECT_TRUE(status_contains(*lobby, "[host]"));
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
