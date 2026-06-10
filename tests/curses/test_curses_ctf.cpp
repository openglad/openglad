/* Curses-client CTF presentation and replication: terminal glyphs for flags
 * and control points, the HUD caps/FLAG/RESPAWN line read from a mirror
 * world's replicated CtfState, and an in-process 2-client round where the
 * host's authoritative server world is stamped CTF and both mirrors converge.
 */
#include <gtest/gtest.h>

#include <openglad/platform/curses/curses_network.h>
#include <openglad/platform/curses/curses_renderer.h>
#include <openglad/platform/curses/glyph_map.h>
#include <openglad/platform/curses/headless_terminal.h>
#include <openglad/platform/curses/clock.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/core/order.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/ctf/ctf_state.h>
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
bool curses_network_testing_inject_ctf(CursesGameSession& session,
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
    save.current_campaign = "org.openglad.gladiator";
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

TEST(CursesCtf, ctf_constants_visible_without_sdl)
{
    ASSERT_EQ(13, og::FAMILY_FLAG);
    ASSERT_EQ(14, og::FAMILY_CTF_POINT);
    ASSERT_EQ(3, og::sim::kCtfDefaultCaptureLimit);
}

TEST(CursesCtf, glyphs_for_flag_and_control_point_take_team_colors)
{
    const Glyph flag = entity_glyph(Order::Treasure, og::FAMILY_FLAG,
                                    /*team=*/1, false, 0);
    EXPECT_EQ('F', flag.ascii);
    EXPECT_EQ(team_color(1), flag.fg) << "flag tints by its owning team";
    EXPECT_TRUE(flag.bold);
    EXPECT_FALSE(flag.skip);

    const Glyph point = entity_glyph(Order::Treasure, og::FAMILY_CTF_POINT,
                                     /*team=*/2, false, 0);
    EXPECT_EQ('O', point.ascii);
    EXPECT_EQ(team_color(2), point.fg) << "control point tints by its owner";
    EXPECT_FALSE(point.skip);

    // Classic treasures keep their table glyphs.
    const Glyph exit_glyph = entity_glyph(Order::Treasure, FAMILY_EXIT, 0, false, 0);
    EXPECT_EQ('>', exit_glyph.ascii);
}

TEST(CursesCtf, hud_shows_caps_line_with_flag_and_respawn_markers)
{
    HandWorld hw(16, 16);
    walker* hero = hw.add_entity(Order::Living, FAMILY_SOLDIER, 8, 8, 0);
    ASSERT_NE(nullptr, hero);
    const std::uint32_t id = hero->entity_id();
    hw.world().my_team = 0;

    // Mirror-style replicated CTF state (the renderer reads it directly).
    og::sim::CtfState& ctf = hw.world().ctf;
    ctf.active = true;
    ctf.team_active[0] = true;
    ctf.team_active[1] = true;
    ctf.captures[0] = 1;
    ctf.captures[1] = 2;

    HeadlessTerminal term(21, 60);
    CursesRenderer renderer;
    renderer.draw(term, hw.world(), id);
    std::string row1 = term.text_row(1);
    EXPECT_NE(row1.find("Caps 1:2"), std::string::npos)
        << "caps line on HUD row 1: " << row1;
    EXPECT_EQ(row1.find("FLAG"), std::string::npos)
        << "no carrier marker without a carried flag: " << row1;

    // The followed avatar picks up team 1's flag.
    ctf.flags[1].state = og::sim::CtfFlagState::Carried;
    ctf.flags[1].carrier_entity_id = id;
    renderer.draw(term, hw.world(), id);
    row1 = term.text_row(1);
    EXPECT_NE(row1.find("FLAG"), std::string::npos)
        << "carrier marker expected: " << row1;

    // The followed avatar dies into the respawn queue.
    ctf.flags[1].state = og::sim::CtfFlagState::AtHome;
    ctf.flags[1].carrier_entity_id = 0;
    og::sim::CtfRespawnEntry entry;
    entry.kind = 0;
    entry.ticks_left = 36; // 3 s at 12 ticks/s
    entry.walker_entity_id = id;
    ctf.respawn_queue.push_back(entry);
    renderer.draw(term, hw.world(), id);
    row1 = term.text_row(1);
    EXPECT_NE(row1.find("RESPAWN 3"), std::string::npos)
        << "respawn countdown expected: " << row1;

    // Inactive CTF leaves the classic HUD untouched.
    ctf.active = false;
    renderer.draw(term, hw.world(), id);
    row1 = term.text_row(1);
    EXPECT_EQ(row1.find("Caps"), std::string::npos)
        << "no caps line on classic levels: " << row1;
}

TEST(CursesCtf, two_client_round_replicates_ctf_state_to_both_mirrors)
{
    SaveData host_save;
    SaveData join_save;
    init_team_save(host_save, 0, FAMILY_SOLDIER, "Host");
    init_team_save(join_save, 1, FAMILY_ELF, "Joiner");

    StartedGame game = negotiate_and_start(host_save, join_save);
    ASSERT_NE(game.host_session, nullptr) << "host session should start";
    ASSERT_NE(game.join_session, nullptr) << "join session should start";

    // Stamp the authoritative server world CTF (flags + anchors + type bit)
    // before the match ticks; the next snapshots replicate everything.
    ASSERT_TRUE(curses_network_testing_inject_ctf(*game.host_session,
                                                  /*respawn_ticks=*/24));

    advance_all(*game.host_session, *game.join_session, 40);

    const GameWorld& host_mirror = game.host_session->mirror_world();
    const GameWorld& join_mirror = game.join_session->mirror_world();

    ASSERT_TRUE(host_mirror.ctf.active)
        << "host mirror must see the activated CTF match";
    ASSERT_TRUE(join_mirror.ctf.active)
        << "join mirror must see the activated CTF match";
    EXPECT_TRUE(host_mirror.ctf.flags[0].present);
    EXPECT_TRUE(host_mirror.ctf.flags[1].present);
    EXPECT_TRUE(join_mirror.ctf.flags[0].present);
    EXPECT_TRUE(join_mirror.ctf.flags[1].present);
    EXPECT_EQ(24, host_mirror.ctf.respawn_ticks)
        << "the injected respawn config must replicate";
    EXPECT_EQ(24, join_mirror.ctf.respawn_ticks);
    for (int team = 0; team < 4; ++team) {
        EXPECT_EQ(host_mirror.ctf.captures[team], join_mirror.ctf.captures[team])
            << "capture counts must agree between mirrors (team " << team << ")";
    }

    // The joiner's HUD renders the caps line straight off its mirror.
    HeadlessTerminal term(21, 70);
    CursesRenderer renderer;
    renderer.draw(term, game.join_session->mirror_world(),
                  game.join_session->followed_entity_id());
    const std::string row1 = term.text_row(1);
    EXPECT_NE(row1.find("Caps"), std::string::npos)
        << "caps line should render from the join mirror: " << row1;
}
