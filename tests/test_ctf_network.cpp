// Networked CTF replication: server-authoritative matches must converge on
// every client through lazy init, flag carries, respawn cycles, and match
// end, with serialization validation round-tripping every message; and the
// CTF gate must suppress the team-wipe endgame that classic levels keep.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstdint>

#include "test_network_fixture.h"

namespace {

using og::sim::test::NetworkTestConfig;
using og::sim::test::NetworkTestFixture;

struct CtfNetScenario {
    walker* flag0 = nullptr;
    walker* flag1 = nullptr;
};

walker* spawn_server_flag(GameWorld& world, int team, int x, int y)
{
    walker* flag = world.add_fx_ob(Order::Treasure, og::FAMILY_FLAG);
    if (flag == nullptr)
        return nullptr;
    flag->setxy(x, y);
    flag->set_team_num(static_cast<unsigned char>(team));
    return flag;
}

walker* spawn_server_anchor(GameWorld& world, int team, int x, int y)
{
    walker* marker = world.add_ob(Order::Special, FAMILY_RESERVED_TEAM);
    if (marker == nullptr)
        return nullptr;
    marker->setxy(x, y);
    marker->set_team_num(static_cast<unsigned char>(team));
    return marker;
}

// Turns the loaded classic level into a CTF map on the authoritative server:
// flags and respawn anchors for teams 0/1 plus the TYPE_CTF bit. Clients keep
// loading the classic level; everything CTF reaches them via replication.
CtfNetScenario inject_ctf_scenario(NetworkTestFixture& fixture,
                                   std::size_t client_count,
                                   short requested_respawn_ticks)
{
    CtfNetScenario scenario;
    fixture.with_server_context([&] {
        GameWorld& world = fixture.server_world();
        world.type |= GameWorld::TYPE_CTF;
        if (requested_respawn_ticks > 0)
            world.ctf_requested_respawn_ticks = requested_respawn_ticks;

        const int far_x = std::max(160, static_cast<int>(world.pixmaxx) - 48);
        const int far_y = std::max(160, static_cast<int>(world.pixmaxy) - 48);
        scenario.flag0 = spawn_server_flag(world, 0, 48, 48);
        scenario.flag1 = spawn_server_flag(world, 1, far_x, far_y);
        spawn_server_anchor(world, 0, 80, 48);
        spawn_server_anchor(world, 0, 48, 80);
        spawn_server_anchor(world, 1, far_x - 32, far_y);
        spawn_server_anchor(world, 1, far_x, far_y - 32);
    });

    // The level-type bit is authored, not replicated, so mirror it on the
    // client worlds the way a real CTF .fss load would.
    for (std::size_t index = 0; index < client_count; ++index)
    {
        fixture.with_client_context(index, [&] {
            fixture.client_world(index).type |= GameWorld::TYPE_CTF;
        });
    }
    return scenario;
}

int living_count_for_team(const GameWorld& world, unsigned char team)
{
    int count = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* w = uptr.get();
        if (w != nullptr && !w->dead() && w->query_order() == Order::Living &&
            w->team_num() == team)
        {
            count++;
        }
    }
    return count;
}

void wipe_team_on_server(NetworkTestFixture& fixture, unsigned char team)
{
    fixture.with_server_context([&] {
        for (const auto& uptr : fixture.server_world().oblist)
        {
            walker* w = uptr.get();
            if (w != nullptr && !w->dead() &&
                w->query_order() == Order::Living && w->team_num() == team)
            {
                w->set_dead(1);
            }
        }
    });
}

} // namespace

TEST(CtfNetwork, two_client_match_converges_through_full_lifecycle)
{
    NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();
    const CtfNetScenario scenario =
        inject_ctf_scenario(fixture, 2, /*requested_respawn_ticks=*/8);
    ASSERT_NE(nullptr, scenario.flag0);
    ASSERT_NE(nullptr, scenario.flag1);
    fixture.initial_sync();

    // Lazy init runs on the first authoritative tick and must replicate.
    fixture.step_ticks(2);
    ASSERT_TRUE(fixture.server_world().ctf.active);
    fixture.expect_clients_match_server();
    for (int index = 0; index < 2; ++index)
    {
        const GameWorld& client = fixture.client_world(index);
        EXPECT_TRUE(client.ctf.active) << "client " << index;
        EXPECT_TRUE(client.ctf.init_attempted) << "client " << index;
        EXPECT_TRUE(client.ctf.flags[0].present) << "client " << index;
        EXPECT_TRUE(client.ctf.flags[1].present) << "client " << index;
        EXPECT_EQ(8, client.ctf.respawn_ticks) << "client " << index;
        EXPECT_GE(client.ctf.anchor_count[0], 1) << "client " << index;
    }

    // A carried flag replicates: walk the bound control onto the enemy flag.
    walker* runner = fixture.server_control(0);
    ASSERT_NE(nullptr, runner);
    const std::uint32_t runner_id = runner->entity_id();
    fixture.with_server_context([&] {
        runner->setxy(scenario.flag1->xpos(),
                      static_cast<short>(scenario.flag1->ypos() - 16));
        ASSERT_TRUE(scenario.flag1->eat_me(runner));
    });
    ASSERT_EQ(og::sim::CtfFlagState::Carried,
              fixture.server_world().ctf.flags[1].state);
    fixture.step_ticks(1);
    fixture.expect_clients_match_server();
    EXPECT_EQ(og::sim::CtfFlagState::Carried,
              fixture.client_world(0).ctf.flags[1].state);
    EXPECT_EQ(runner_id, fixture.client_world(1).ctf.flags[1].carrier_entity_id);

    // A killed carrier drops the flag and enters the respawn queue; clients
    // track the pending entry and the respawned replacement walker.
    fixture.with_server_context([&] { runner->set_dead(1); });
    fixture.step_ticks(2);
    ASSERT_FALSE(fixture.server_world().ctf.respawn_queue.empty());
    fixture.expect_clients_match_server();
    EXPECT_FALSE(fixture.client_world(0).ctf.respawn_queue.empty());
    EXPECT_NE(og::sim::CtfFlagState::Carried,
              fixture.client_world(0).ctf.flags[1].state);

    fixture.step_ticks(12);
    ASSERT_TRUE(fixture.server_world().ctf.respawn_queue.empty())
        << "respawn should have fired within the configured 8 ticks";
    fixture.expect_clients_match_server();

    // Match end: force the capture limit and let the win check run.
    fixture.with_server_context([&] {
        GameWorld& world = fixture.server_world();
        world.ctf.captures[0] = world.ctf.capture_limit;
    });
    fixture.step_ticks(1);
    ASSERT_TRUE(fixture.server_world().game_ended);
    ASSERT_EQ(0, fixture.server_world().ctf.winner_team);
    fixture.expect_clients_match_server();
    for (int index = 0; index < 2; ++index)
    {
        const GameWorld& client = fixture.client_world(index);
        EXPECT_TRUE(client.game_ended) << "client " << index;
        EXPECT_EQ(0, client.ctf.winner_team) << "client " << index;
        EXPECT_EQ(client.ctf.capture_limit, client.ctf.captures[0])
            << "client " << index;
    }
}

TEST(CtfNetwork, four_client_match_state_replicates)
{
    NetworkTestFixture fixture({
        .player_count = 4,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();
    const CtfNetScenario scenario = inject_ctf_scenario(fixture, 4, 0);
    ASSERT_NE(nullptr, scenario.flag0);
    fixture.initial_sync();

    fixture.step_ticks(20);
    ASSERT_TRUE(fixture.server_world().ctf.active);
    fixture.expect_clients_match_server();
    for (int index = 0; index < 4; ++index)
    {
        const GameWorld& client = fixture.client_world(index);
        EXPECT_TRUE(client.ctf.active) << "client " << index;
        EXPECT_EQ(fixture.server_world().ctf.team_count, client.ctf.team_count)
            << "client " << index;
        EXPECT_EQ(fixture.server_world().ctf.respawn_serial,
                  client.ctf.respawn_serial)
            << "client " << index;
    }
}

TEST(CtfNetwork, team_wipe_is_suppressed_during_ctf_match)
{
    NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();
    // A respawn wait far beyond the observation window keeps the bound team
    // fully dead while the gate is exercised.
    inject_ctf_scenario(fixture, 2, /*requested_respawn_ticks=*/600);
    fixture.initial_sync();

    fixture.step_ticks(2);
    ASSERT_TRUE(fixture.server_world().ctf.active);

    const unsigned char bound_team =
        static_cast<unsigned char>(fixture.server_world().my_team);
    ASSERT_GT(living_count_for_team(fixture.server_world(), bound_team), 0);
    wipe_team_on_server(fixture, bound_team);
    ASSERT_EQ(0, living_count_for_team(fixture.server_world(), bound_team));

    const std::uint32_t tick_before = fixture.server_world().tick_count_;
    fixture.step_ticks(6);
    EXPECT_EQ(tick_before + 6, fixture.server_world().tick_count_)
        << "an active CTF match must keep ticking through a team wipe";
    EXPECT_FALSE(fixture.server_world().game_ended);
    EXPECT_EQ(0, fixture.server_world().ending);
    EXPECT_FALSE(fixture.server_world().ctf.respawn_queue.empty())
        << "the wiped walkers should be waiting on the respawn queue";
    fixture.expect_clients_match_server();
    EXPECT_FALSE(fixture.client_world(0).game_ended);
}

TEST(CtfNetwork, team_wipe_still_ends_classic_level)
{
    NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();
    fixture.initial_sync();

    fixture.step_ticks(2);
    ASSERT_FALSE(fixture.server_world().type & GameWorld::TYPE_CTF);

    const unsigned char bound_team =
        static_cast<unsigned char>(fixture.server_world().my_team);
    ASSERT_GT(living_count_for_team(fixture.server_world(), bound_team), 0);
    wipe_team_on_server(fixture, bound_team);

    const std::uint32_t tick_before = fixture.server_world().tick_count_;
    fixture.step_ticks(6);
    EXPECT_EQ(tick_before, fixture.server_world().tick_count_)
        << "a classic team wipe must stop the authoritative tick";
    EXPECT_EQ(1, fixture.server_world().ending)
        << "a classic team wipe must request the loss endgame";
}
