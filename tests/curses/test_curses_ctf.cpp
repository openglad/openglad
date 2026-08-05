/* Curses-client CTF presentation and replication: terminal glyphs for flags
 * and control points, the HUD caps/FLAG/RESPAWN line read from a mirror
 * world's replicated ModeState, and an in-process 2-client round where the
 * host's authoritative server world is stamped CTF and both mirrors converge.
 */
#include <gtest/gtest.h>

#include <cstring>

#include <openglad/platform/curses/curses_network.h>
#include <openglad/platform/curses/curses_renderer.h>
#include <openglad/platform/curses/glyph_map.h>
#include <openglad/platform/curses/headless_terminal.h>
#include <openglad/platform/curses/clock.h>

#include <openglad/core/constants.h>
#include <openglad/core/campaign_ids.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/net_transport_inprocess.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/ui/picker_common.h>
#include <openglad/resources/save_data.h>

#include <memory>
#include <string>

using namespace og::curses;

// Test-only construction hooks exported from curses_network.cpp (same pattern
// as tests/curses/test_curses_network.cpp).
namespace og::curses {
std::unique_ptr<CursesLobby> make_host_lobby_over_transport_for_testing(
    SaveData& save, int difficulty,
    std::shared_ptr<og::sim::ITransport> combined_transport,
    std::shared_ptr<og::sim::InProcessTransport> host_client_transport);
std::unique_ptr<CursesLobby> make_join_lobby_over_transport_for_testing(
    SaveData& save, int difficulty,
    std::shared_ptr<og::sim::ITransport> transport,
    og::sim::PeerId server_peer_id);
bool curses_network_testing_inject_mode(CursesGameSession& session,
                                       short requested_respawn_ticks);
} // namespace og::curses

namespace {

// Minimal hand-built world (the test_curses_renderer pattern): a controlled
// entity factory plus an all-grass grid, so the renderer can be driven without
// a level loader.
class HandWorld
{
public:
    HandWorld(int tiles_w, int tiles_h)
        : world_(std::make_unique<GameWorld>(0)), w_(tiles_w), h_(tiles_h)
    {
        world_->entity_factory = [](Order, std::int32_t) {
            return std::make_unique<walker>();
        };
        auto* cells = new unsigned char[static_cast<std::size_t>(w_ * h_)];
        for (int i = 0; i < w_ * h_; ++i)
            cells[i] = PIX_GRASS1;
        world_->grid = PixieData(1, static_cast<unsigned char>(w_),
                                 static_cast<unsigned char>(h_), cells);
        world_->mysmoother.set_target(world_->grid);
        world_->pixmaxx = w_ * GRID_SIZE;
        world_->pixmaxy = h_ * GRID_SIZE;
    }

    GameWorld& world() { return *world_; }

    walker* add_entity(Order order, int family, int tx, int ty, unsigned char team)
    {
        walker* w = world_->add_ob(order, family);
        if (w) {
            w->set_order(order);
            w->set_family(static_cast<char>(family));
            w->setxy(static_cast<short>(tx * GRID_SIZE),
                     static_cast<short>(ty * GRID_SIZE));
            w->set_team_num(team);
        }
        return w;
    }

private:
    std::unique_ptr<GameWorld> world_;
    int w_;
    int h_;
};

void init_team_save(SaveData& save, short team, char family, const char* name)
{
    save.current_campaign = "gladiator";
    save.scen_num = 1;
    save.numplayers = 1;
    save.my_team = team;
    save.allied_mode = 0;
    og::ui::initialize_starting_team(save, {family});
    for (auto& member : save.team_list) {
        if (member) {
            member->teamnum = team;
            if (name)
                member->name = name;
        }
    }
}

struct StartedGame {
    std::unique_ptr<CursesLobby> host_lobby;
    std::unique_ptr<CursesLobby> join_lobby;
    std::unique_ptr<CursesGameSession> host_session;
    std::unique_ptr<CursesGameSession> join_session;
};

StartedGame negotiate_and_start(SaveData& host_save, SaveData& join_save)
{
    auto server = og::sim::InProcessTransport::create_server();
    server->accept_connections();
    auto host_client = server->create_client_transport();
    auto join_client = server->create_client_transport();

    StartedGame game;
    game.host_lobby = make_host_lobby_over_transport_for_testing(
        host_save, /*difficulty=*/1, server, host_client);
    game.join_lobby = make_join_lobby_over_transport_for_testing(
        join_save, /*difficulty=*/1, join_client, join_client->local_peer_id());

    HeadlessTerminal host_term(24, 80);
    HeadlessTerminal join_term(24, 80);
    FakeClock clock;

    bool rostered = false;
    for (int i = 0; i < 200 && !rostered; ++i) {
        game.host_lobby->poll(host_term, clock);
        game.join_lobby->poll(join_term, clock);
        for (const std::string& s : game.host_lobby->status_lines())
            if (s == "Players: 2")
                rostered = true;
    }

    // §4.3 ready gate: the joiner must be ready before the host may start.
    (void)game.join_lobby->set_ready(true);
    for (int i = 0; i < 200; ++i) {
        game.host_lobby->poll(host_term, clock);
        game.join_lobby->poll(join_term, clock);
        bool joiner_ready = false;
        for (const og::sim::LobbyPlayer& player : game.host_lobby->players())
            if (!player.is_host && player.ready)
                joiner_ready = true;
        if (joiner_ready)
            break;
    }

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

void advance_all(CursesGameSession& host, CursesGameSession& join, int frames)
{
    for (int i = 0; i < frames; ++i) {
        InputState idle;
        host.send_input(idle);
        join.send_input(idle);
        host.advance();
        join.advance();
    }
}

} // namespace

TEST(CursesCtf, hud_shows_mode_lines_with_respawn_marker)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_entity(Order::Living, FAMILY_SOLDIER, 8, 8, 0);
    ASSERT_NE(nullptr, hero);
    const std::uint32_t id = hero->entity_id();
    hw.world().my_team = 0;

    // Mirror-style replicated mode state (the renderer reads it directly).
    hw.world().type |= GameWorld::TYPE_SCRIPTED;
    og::sim::ModeState& mode = hw.world().mode;
    mode.active = true;
    mode.init_attempted = true;
    std::strncpy(mode.name.data(), "CTF", mode.name.size() - 1);
    mode.hud[0].team = 0;
    std::strncpy(mode.hud[0].text.data(), "Caps 1:2",
                 mode.hud[0].text.size() - 1);

    HeadlessTerminal term(21, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);
    std::string row1 = term.text_row(1);
    EXPECT_NE(row1.find("CTF"), std::string::npos)
        << "mode name on HUD row 1: " << row1;
    EXPECT_NE(row1.find("Caps 1:2"), std::string::npos)
        << "mode HUD line on HUD row 1: " << row1;

    // Priority order: the mode fields ride ahead of the score tail, so
    // narrow terminals clip the low-value end.
    ASSERT_NE(row1.find("Score"), std::string::npos) << row1;
    EXPECT_LT(row1.find("Caps"), row1.find("Score")) << row1;

    // A terminal too narrow for the whole line keeps the mode text and
    // loses Score.
    const int narrow_cols = static_cast<int>(row1.find("Caps 1:2")) + 12;
    HeadlessTerminal narrow(21, narrow_cols);
    renderer.draw(narrow, hw.world(), id);
    const std::string narrow_row1 = narrow.text_row(1);
    EXPECT_NE(narrow_row1.find("Caps 1:2"), std::string::npos) << narrow_row1;
    EXPECT_EQ(narrow_row1.find("Score"), std::string::npos) << narrow_row1;

    // The followed avatar dies into the respawn queue.
    og::sim::RespawnEntry entry;
    entry.kind = 0;
    entry.ticks_left = 36; // 3 s at 12 ticks/s
    entry.walker_entity_id = id;
    hw.world().respawn.respawn_queue.push_back(entry);
    renderer.draw(term, hw.world(), id);
    row1 = term.text_row(1);
    EXPECT_NE(row1.find("RESPAWN 3"), std::string::npos)
        << "respawn countdown expected: " << row1;
    hw.world().respawn.respawn_queue.clear();

    HeadlessTerminal wide(21, 90);
    renderer.draw(wide, hw.world(), id);
    row1 = wide.text_row(1);
    EXPECT_NE(row1.find("Caps 1:2"), std::string::npos) << row1;

    // An inactive mode leaves the classic HUD untouched.
    mode.active = false;
    renderer.draw(term, hw.world(), id);
    row1 = term.text_row(1);
    EXPECT_EQ(row1.find("Caps"), std::string::npos)
        << "no mode lines on classic levels: " << row1;
}

TEST(CursesCtf, two_client_round_replicates_mode_state_to_both_mirrors)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    StartedGame game = negotiate_and_start(host_save, join_save);
    ASSERT_NE(game.host_session, nullptr) << "host session should start";
    ASSERT_NE(game.join_session, nullptr) << "join session should start";

    // Stamp the authoritative server world scripted (armed ModeState + HUD
    // line + anchors + type bit) before the match ticks; the next snapshots
    // replicate everything through the v10 mode block.
    ASSERT_TRUE(curses_network_testing_inject_mode(*game.host_session,
                                                  /*respawn_ticks=*/24));
    // The level-type bit is authored, not replicated: a real scripted .fss
    // load stamps it on every peer, so mirror it on the joiner too.
    game.join_session->mirror_world().type |= GameWorld::TYPE_SCRIPTED;

    advance_all(*game.host_session, *game.join_session, 40);

    const GameWorld& host_mirror = game.host_session->mirror_world();
    const GameWorld& join_mirror = game.join_session->mirror_world();

    ASSERT_TRUE(host_mirror.mode.active)
        << "host mirror must see the activated scripted match";
    ASSERT_TRUE(join_mirror.mode.active)
        << "join mirror must see the activated scripted match";
    EXPECT_STREQ("CTF", host_mirror.mode.name.data());
    EXPECT_STREQ("CTF", join_mirror.mode.name.data());
    EXPECT_STREQ("Caps 0:0", host_mirror.mode.hud[0].text.data());
    EXPECT_STREQ("Caps 0:0", join_mirror.mode.hud[0].text.data());
    EXPECT_EQ(24, host_mirror.respawn.respawn_ticks)
        << "the injected respawn config must replicate";
    EXPECT_EQ(24, join_mirror.respawn.respawn_ticks);
    for (int team = 0; team < 4; ++team) {
        EXPECT_EQ(host_mirror.mode.vars[team], join_mirror.mode.vars[team])
            << "mode vars must agree between mirrors (team " << team << ")";
    }
    EXPECT_GE(join_mirror.respawn.anchor_count[0], 1)
        << "the anchor scan must replicate through the respawn block";

    // The joiner's HUD renders the mode name + scoreboard line straight off
    // its mirror.
    HeadlessTerminal term(21, 70);
    CursesRenderer renderer;
    renderer.draw(term, game.join_session->mirror_world(),
                  game.join_session->followed_entity_id());
    const std::string row1 = term.text_row(1);
    EXPECT_NE(row1.find("CTF"), std::string::npos)
        << "mode name should render from the join mirror: " << row1;
    EXPECT_NE(row1.find("Caps 0:0"), std::string::npos)
        << "mode HUD line should render from the join mirror: " << row1;
}
