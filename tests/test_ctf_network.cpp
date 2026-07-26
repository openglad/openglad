// Networked CTF replication: server-authoritative matches must converge on
// every client through lazy init, flag carries, respawn cycles, and match
// end, with serialization validation round-tripping every message; and the
// CTF gate must suppress the team-wipe endgame that classic levels keep.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/core/ctf_constants.h>
#include <openglad/gameplay/ctf/ctf_state.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/sim_control_policy.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstdint>
#include <memory>

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

// Makes the bound players the SOLE livings of their team (the FIRST BLOOD
// shape): every other walker on that team moves to `enemy_team`, so a death
// leaves the player with no fallback body and the server's control nulls.
void isolate_bound_players_on_team(NetworkTestFixture& fixture,
                                   unsigned char team,
                                   unsigned char enemy_team,
                                   std::size_t player_count)
{
    fixture.with_server_context([&] {
        for (const auto& uptr : fixture.server_world().oblist)
        {
            walker* w = uptr.get();
            if (w == nullptr || w->dead() ||
                w->query_order() != Order::Living || w->team_num() != team)
            {
                continue;
            }
            bool is_bound_control = false;
            for (std::size_t p = 0; p < player_count; ++p)
            {
                if (fixture.server_control(p) == w)
                    is_bound_control = true;
            }
            if (!is_bound_control)
                w->set_team_num(enemy_team);
        }
    });
}

// Stages the owner-locked networked-allied pair used by the §4.4 enforcement
// tests: both bound heroes owner-tagged to their machines (player 0 =
// machine 0, player 1 = machine 1, both deployed), the bound team isolated,
// and one SPARE unclaimed hero owned by player 1 — the walker the legacy
// shared pool WOULD hand player 0.
struct OwnerLockedAlliedStage {
    walker* hero0 = nullptr;
    walker* hero1 = nullptr;
    walker* spare = nullptr;
};

OwnerLockedAlliedStage stage_owner_locked_allied_pair(NetworkTestFixture& fixture)
{
    OwnerLockedAlliedStage stage;
    stage.hero0 = fixture.server_control(0);
    stage.hero1 = fixture.server_control(1);
    if (stage.hero0 == nullptr || stage.hero1 == nullptr)
        return stage;

    const unsigned char bound_team = stage.hero0->team_num();
    const unsigned char enemy_team = bound_team == 0 ? 1 : 0;
    fixture.with_server_context([&] {
        fixture.server_world().allied_mode = 1;
        stage.hero0->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        stage.hero0->myguy->id = 71;
        stage.hero0->myguy->owner_player_index = 0;
        stage.hero1->set_owned_myguy(std::make_unique<guy>(FAMILY_ELF));
        stage.hero1->myguy->id = 72;
        stage.hero1->myguy->owner_player_index = 1;
    });
    isolate_bound_players_on_team(fixture, bound_team, enemy_team, 2);

    fixture.with_server_context([&] {
        stage.spare =
            fixture.server_world().add_ob(Order::Living, FAMILY_SOLDIER);
        if (stage.spare != nullptr)
        {
            stage.spare->setxy(static_cast<short>(stage.hero1->xpos() + 32),
                               static_cast<short>(stage.hero1->ypos()));
            stage.spare->set_team_num(bound_team);
            stage.spare->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
            stage.spare->myguy->id = 73;
            stage.spare->myguy->owner_player_index = 1;
        }

        std::array<std::uint8_t, og::sim::kPlayerMachineSlots> machines;
        machines.fill(og::sim::kPlayerMachineNone);
        machines[0] = og::sim::encode_player_machine(0, true);
        machines[1] = og::sim::encode_player_machine(1, true);
        og::sim::set_control_policy(fixture.server_world(),
                                    og::sim::kControlPolicyOwnerLocked,
                                    machines);
    });
    return stage;
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

// A solo-team player's assignment remains on the corpse while the CTF
// respawn is pending, so the camera can follow the respawn and the player
// resumes control without a control-change round trip.
TEST(CtfNetwork, solo_team_player_retains_control_through_respawn)
{
    NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();
    inject_ctf_scenario(fixture, 1, /*requested_respawn_ticks=*/8);

    walker* player = fixture.server_control(0);
    ASSERT_NE(nullptr, player);
    const std::uint32_t player_id = player->entity_id();
    fixture.with_server_context([&] {
        player->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        player->myguy->id = 31;
    });
    const unsigned char bound_team = player->team_num();
    const unsigned char enemy_team = bound_team == 0 ? 1 : 0;
    isolate_bound_players_on_team(fixture, bound_team, enemy_team, 1);

    fixture.initial_sync();
    fixture.step_ticks(2);
    ASSERT_TRUE(fixture.server_world().ctf.active);

    // With respawns enabled, death retains the controlled entity on both the
    // server and client while the countdown is pending.
    fixture.with_server_context([&] { player->set_dead(1); });
    fixture.step_ticks(2);
    ASSERT_TRUE(og::sim::ctf_pending_player_respawn(
        fixture.server_world().ctf, player_id))
        << "the death scan should schedule the player corpse";
    ASSERT_EQ(player, fixture.server_control(0));
    ASSERT_EQ(player_id, fixture.client(0).controlled_entity_ids()[0]);

    // Far past the 8-tick timer, the same assignment remains after revive.
    fixture.step_ticks(40);
    walker* revived = fixture.server_world().find_by_id(player_id);
    ASSERT_NE(nullptr, revived);
    ASSERT_FALSE(revived->dead()) << "the sim revive must have fired";
    EXPECT_EQ(0, revived->user()) << "revive preserves the player's user tag";
    EXPECT_EQ(revived, fixture.server_control(0));
    EXPECT_EQ(player_id, fixture.client(0).controlled_entity_ids()[0]);
    fixture.expect_clients_match_server();
}

// Two same-team players who die together retain their exact assignments;
// respawn processing must never swap their characters.
TEST(CtfNetwork, two_same_team_dead_players_retain_their_own_characters)
{
    NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();
    inject_ctf_scenario(fixture, 2, /*requested_respawn_ticks=*/8);

    walker* player0 = fixture.server_control(0);
    walker* player1 = fixture.server_control(1);
    ASSERT_NE(nullptr, player0);
    ASSERT_NE(nullptr, player1);
    ASSERT_NE(player0, player1);
    ASSERT_EQ(player0->team_num(), player1->team_num())
        << "both fixture players should bind onto the level's my_team";
    const std::uint32_t id0 = player0->entity_id();
    const std::uint32_t id1 = player1->entity_id();
    fixture.with_server_context([&] {
        player0->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        player0->myguy->id = 41;
        player1->set_owned_myguy(std::make_unique<guy>(FAMILY_ELF));
        player1->myguy->id = 42;
    });
    const unsigned char bound_team = player0->team_num();
    const unsigned char enemy_team = bound_team == 0 ? 1 : 0;
    isolate_bound_players_on_team(fixture, bound_team, enemy_team, 2);

    fixture.initial_sync();
    fixture.step_ticks(2);
    ASSERT_TRUE(fixture.server_world().ctf.active);

    fixture.with_server_context([&] {
        player0->set_dead(1);
        player1->set_dead(1);
    });
    fixture.step_ticks(2);
    ASSERT_EQ(player0, fixture.server_control(0));
    ASSERT_EQ(player1, fixture.server_control(1));
    ASSERT_EQ(id0, fixture.client(0).controlled_entity_ids()[0]);
    ASSERT_EQ(id1, fixture.client(0).controlled_entity_ids()[1]);
    ASSERT_EQ(id0, fixture.client(1).controlled_entity_ids()[0]);
    ASSERT_EQ(id1, fixture.client(1).controlled_entity_ids()[1]);

    fixture.step_ticks(40);
    walker* revived0 = fixture.server_world().find_by_id(id0);
    walker* revived1 = fixture.server_world().find_by_id(id1);
    ASSERT_NE(nullptr, revived0);
    ASSERT_NE(nullptr, revived1);
    ASSERT_FALSE(revived0->dead());
    ASSERT_FALSE(revived1->dead());
    EXPECT_EQ(revived0, fixture.server_control(0))
        << "player 0 must reclaim its own character (exact user tag)";
    EXPECT_EQ(revived1, fixture.server_control(1))
        << "player 1 must reclaim its own character (exact user tag)";
    EXPECT_EQ(id0, fixture.client(0).controlled_entity_ids()[0]);
    EXPECT_EQ(id1, fixture.client(0).controlled_entity_ids()[1]);
    EXPECT_EQ(id0, fixture.client(1).controlled_entity_ids()[0]);
    EXPECT_EQ(id1, fixture.client(1).controlled_entity_ids()[1]);
    fixture.expect_clients_match_server();
}

TEST(CtfNetwork, bind_player_prefers_owner_matched_heroes_on_shared_team)
{
    // Two players share team 0 (the CTF multi-human-per-team shape). Each
    // must bind to the walker whose myguy carries ITS owner tag, not to the
    // first unclaimed walker in oblist order.
    NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
    });
    fixture.load_level();

    walker* first = nullptr;
    walker* second = nullptr;
    fixture.with_server_context([&] {
        // The owner preference is CTF-scoped: classic/allied worlds keep the
        // original shared-pool claim, so mark the server world as CTF first.
        fixture.server_world().type |= GameWorld::TYPE_CTF;
        for (const auto& uptr : fixture.server_world().oblist)
        {
            walker* w = uptr.get();
            if (w == nullptr || w->dead() ||
                w->query_order() != Order::Living || w->team_num() != 0)
            {
                continue;
            }
            if (first == nullptr)
            {
                first = w;
                continue;
            }
            second = w;
            break;
        }
        if (second == nullptr)
        {
            // The classic level fields one team-0 living; add a second hero
            // beside it so the team is genuinely shared.
            second = fixture.server_world().add_ob(Order::Living, FAMILY_SOLDIER);
            ASSERT_NE(nullptr, second);
            second->setxy(static_cast<short>(first->xpos() + 32),
                          static_cast<short>(first->ypos()));
            second->set_team_num(0);
        }

        // Tag in REVERSE order: the earlier walker belongs to player 1, the
        // later one to player 0. The generic first-unclaimed scan would give
        // player 0 the earlier walker; the owner preference must not.
        first->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        first->myguy->id = 21;
        first->myguy->owner_player_index = 1;
        first->myguy->owner_save_slot = 0;
        second->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        second->myguy->id = 22;
        second->myguy->owner_player_index = 0;
        second->myguy->owner_save_slot = 1;
    });
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);

    fixture.rebind_players();

    EXPECT_EQ(second, fixture.server_control(0))
        << "player 0 must claim its owner-tagged hero";
    EXPECT_EQ(first, fixture.server_control(1))
        << "player 1 must claim its owner-tagged hero";
    EXPECT_EQ(0, fixture.server_control(0)->user());
    EXPECT_EQ(1, fixture.server_control(1)->user());
}

TEST(CtfNetwork, multi_seat_peer_binds_each_seat_to_its_owner_tagged_hero)
{
    // ONE physical peer carrying TWO local seats (local_slot 0/1) in a CTF
    // lobby: each seat must claim the walker whose myguy carries ITS OWN
    // owner tag (the per-seat CTF owner preference), not the first unclaimed
    // walker in oblist order.
    NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
    });
    fixture.load_level();
    const og::sim::PeerId peer_id = fixture.client_transport(0).local_peer_id();

    walker* first = nullptr;
    walker* second = nullptr;
    fixture.with_server_context([&] {
        fixture.server_world().type |= GameWorld::TYPE_CTF;
        for (const auto& uptr : fixture.server_world().oblist)
        {
            walker* w = uptr.get();
            if (w == nullptr || w->dead() ||
                w->query_order() != Order::Living || w->team_num() != 0)
            {
                continue;
            }
            if (first == nullptr)
            {
                first = w;
                continue;
            }
            second = w;
            break;
        }
        if (second == nullptr)
        {
            second = fixture.server_world().add_ob(Order::Living, FAMILY_SOLDIER);
            ASSERT_NE(nullptr, second);
            second->setxy(static_cast<short>(first->xpos() + 32),
                          static_cast<short>(first->ypos()));
            second->set_team_num(0);
        }

        // Tag in REVERSE order: the earlier walker belongs to seat 1 (global
        // player 1), the later one to seat 0. A first-unclaimed scan would
        // hand seat 0 the earlier walker; the owner preference must not.
        first->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        first->myguy->id = 61;
        first->myguy->owner_player_index = 1;
        first->myguy->owner_save_slot = 0;
        second->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        second->myguy->id = 62;
        second->myguy->owner_player_index = 0;
        second->myguy->owner_save_slot = 1;
    });
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);

    // Re-run seat 0's claim with the tags staged, then add the SAME peer's
    // second seat: player_index 1 rides local_slot 1 of the one connection.
    fixture.rebind_players();
    fixture.with_server_context([&] {
        fixture.server().bind_player(peer_id, 1u, 0, nullptr, 1u);
    });

    EXPECT_EQ(second, fixture.server_control(0))
        << "seat 0 must claim its owner-tagged hero";
    EXPECT_EQ(first, fixture.server_control(1))
        << "seat 1 must claim its owner-tagged hero";
    EXPECT_EQ(0, fixture.server_control(0)->user());
    EXPECT_EQ(1, fixture.server_control(1)->user());
}

TEST(CtfNetwork, multi_seat_peer_retains_every_seat_through_respawn)
{
    // Both seats of ONE peer retain their own characters through death and
    // revive, including the shared client mirror's per-player mapping.
    NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();
    inject_ctf_scenario(fixture, 1, /*requested_respawn_ticks=*/8);
    const og::sim::PeerId peer_id = fixture.client_transport(0).local_peer_id();

    walker* seat0 = fixture.server_control(0);
    ASSERT_NE(nullptr, seat0);
    walker* seat1 = nullptr;
    fixture.with_server_context([&] {
        seat1 = fixture.server_world().add_ob(Order::Living, FAMILY_ELF);
        ASSERT_NE(nullptr, seat1);
        seat1->setxy(static_cast<short>(seat0->xpos() + 32), seat0->ypos());
        seat1->set_team_num(seat0->team_num());

        seat0->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        seat0->myguy->id = 51;
        seat1->set_owned_myguy(std::make_unique<guy>(FAMILY_ELF));
        seat1->myguy->id = 52;

        // Second seat of the SAME peer (local_slot 1 -> global player 1).
        fixture.server().bind_player(
            peer_id, 1u, static_cast<short>(seat0->team_num()), seat1, 1u);
    });
    ASSERT_EQ(seat1, fixture.server_control(1));
    const std::uint32_t id0 = seat0->entity_id();
    const std::uint32_t id1 = seat1->entity_id();

    const unsigned char bound_team = seat0->team_num();
    const unsigned char enemy_team = bound_team == 0 ? 1 : 0;
    isolate_bound_players_on_team(fixture, bound_team, enemy_team, 2);

    fixture.initial_sync();
    fixture.step_ticks(2);
    ASSERT_TRUE(fixture.server_world().ctf.active);
    ASSERT_EQ(id0, fixture.client(0).controlled_entity_ids()[0]);
    ASSERT_EQ(id1, fixture.client(0).controlled_entity_ids()[1]);

    fixture.with_server_context([&] {
        seat0->set_dead(1);
        seat1->set_dead(1);
    });
    fixture.step_ticks(2);
    ASSERT_EQ(seat0, fixture.server_control(0));
    ASSERT_EQ(seat1, fixture.server_control(1));
    ASSERT_EQ(id0, fixture.client(0).controlled_entity_ids()[0]);
    ASSERT_EQ(id1, fixture.client(0).controlled_entity_ids()[1]);

    fixture.step_ticks(40);
    walker* revived0 = fixture.server_world().find_by_id(id0);
    walker* revived1 = fixture.server_world().find_by_id(id1);
    ASSERT_NE(nullptr, revived0);
    ASSERT_NE(nullptr, revived1);
    ASSERT_FALSE(revived0->dead());
    ASSERT_FALSE(revived1->dead());
    EXPECT_EQ(revived0, fixture.server_control(0));
    EXPECT_EQ(revived1, fixture.server_control(1));
    EXPECT_EQ(0, revived0->user());
    EXPECT_EQ(1, revived1->user());
    EXPECT_EQ(id0, fixture.client(0).controlled_entity_ids()[0]);
    EXPECT_EQ(id1, fixture.client(0).controlled_entity_ids()[1]);
    fixture.expect_clients_match_server();
}

TEST(CtfNetwork, bind_player_generic_claim_when_no_owner_tags)
{
    // Classic shape: no myguy owner tags anywhere. The binding must fall back
    // to the original first-unclaimed scan (regression guard).
    NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
    });
    fixture.load_level();

    walker* control = fixture.server_control(0);
    ASSERT_NE(nullptr, control);
    EXPECT_EQ(0, control->user());
    EXPECT_EQ(0, control->team_num());
}

// [NET-R1] allied claimed-teammates-alive equivalence pin (§4.4/§4.8): under
// the LEGACY control policy (control_policy == 0 — the only policy protocol v8
// ships in WP5), a player whose claimed hero dies while a teammate CLAIMED by
// another player is still alive gets the existing wipe SUPPRESSION, not the
// endgame: has_living_member_for_any_bound_team tracks ALL bound teams
// including the requester's own, so the observable behavior is ControlChange
// entity 0 on every mirror, world.ending == 0, the authoritative tick keeps
// running, and the seat continues null (re-evaluated and re-suppressed every
// tick). WP6's sim_reacquire_control Follow verdict must reproduce EXACTLY
// this under policy-off — this test is the baseline it is diffed against.
TEST(CtfNetwork, allied_claimed_teammate_alive_suppresses_endgame_seat_stays_null)
{
    NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();

    walker* player0 = fixture.server_control(0);
    walker* player1 = fixture.server_control(1);
    ASSERT_NE(nullptr, player0);
    ASSERT_NE(nullptr, player1);
    ASSERT_NE(player0, player1);
    ASSERT_EQ(player0->team_num(), player1->team_num())
        << "both fixture players bind onto the level's my_team";
    const std::uint32_t id1 = player1->entity_id();
    fixture.with_server_context([&] {
        // The shared-seat shape: both fixture players are already on the same
        // bound team and both heroes are OWNED (myguy attached).
        fixture.server_world().allied_mode = 1;
        player0->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        player0->myguy->id = 51;
        player1->set_owned_myguy(std::make_unique<guy>(FAMILY_ELF));
        player1->myguy->id = 52;
    });
    const unsigned char bound_team = player0->team_num();
    const unsigned char enemy_team = bound_team == 0 ? 1 : 0;
    isolate_bound_players_on_team(fixture, bound_team, enemy_team, 2);

    fixture.initial_sync();
    fixture.step_ticks(2);
    ASSERT_FALSE(fixture.server_world().type & GameWorld::TYPE_CTF)
        << "this pins the CLASSIC (non-CTF) suppression path";

    // Player 0's hero dies. The only other living member of the bound team is
    // player 1's CLAIMED hero (user() == 1), which no sim_find_next_control
    // pass may claim (all require user() == -1) — so player 0's reacquire
    // fails and requests the endgame, which the suppression must swallow.
    fixture.with_server_context([&] { player0->set_dead(1); });

    const std::uint32_t tick_before = fixture.server_world().tick_count_;
    fixture.step_ticks(6);
    EXPECT_EQ(tick_before + 6, fixture.server_world().tick_count_)
        << "the suppression must keep the authoritative tick running";
    EXPECT_EQ(0, fixture.server_world().ending)
        << "a claimed teammate alive means NO world ending";
    EXPECT_FALSE(fixture.server_world().game_ended);
    EXPECT_EQ(nullptr, fixture.server_control(0))
        << "the dead player's seat continues null (re-suppressed every tick)";
    EXPECT_EQ(0u, fixture.client(0).controlled_entity_ids()[0])
        << "the ControlChange entity 0 must reach the mirror";
    EXPECT_EQ(player1, fixture.server_control(1))
        << "the living teammate's seat is untouched";
    EXPECT_EQ(id1, fixture.client(0).controlled_entity_ids()[1]);
    EXPECT_EQ(id1, fixture.client(1).controlled_entity_ids()[1]);
    fixture.expect_clients_match_server();

    // Equivalence terminal: once the claimed teammate ALSO dies, no bound team
    // has a living member left and the suppression yields to the loss endgame
    // exactly as team_wipe_still_ends_classic_level pins it.
    fixture.with_server_context([&] { player1->set_dead(1); });
    const std::uint32_t tick_after_suppression =
        fixture.server_world().tick_count_;
    fixture.step_ticks(4);
    EXPECT_EQ(tick_after_suppression, fixture.server_world().tick_count_)
        << "the full wipe must stop the authoritative tick";
    EXPECT_EQ(1, fixture.server_world().ending)
        << "the full wipe must request the loss endgame";
}

// §4.4 site 2 policy-ON, server end to end: under owner-locked a dead
// player's reacquire is DENIED a foreign machine's unclaimed hero — the seat
// goes null (Follow: ControlChange entity 0, no world ending, tick keeps
// running) and the refused hero is never stamped, where the legacy pool
// would have claimed it. The [NET-F3] terminal keeps the wipe endgame
// reachable once every bound-team living falls.
TEST(CtfNetwork, owner_locked_death_rebind_follows_instead_of_claiming_foreign)
{
    NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();

    const OwnerLockedAlliedStage stage =
        stage_owner_locked_allied_pair(fixture);
    ASSERT_NE(nullptr, stage.hero0);
    ASSERT_NE(nullptr, stage.hero1);
    ASSERT_NE(nullptr, stage.spare);
    const std::uint32_t id1 = stage.hero1->entity_id();
    const unsigned char bound_team = stage.hero0->team_num();

    fixture.initial_sync();
    fixture.step_ticks(2);

    // Snapshot v9 replicates the policy scalars to every mirror.
    EXPECT_EQ(og::sim::kControlPolicyOwnerLocked,
              fixture.client_world(0).control_policy);
    EXPECT_EQ(fixture.server_world().player_machine,
              fixture.client_world(1).player_machine);

    // The legacy shared pool WOULD claim the spare for the bound team.
    fixture.with_server_context([&] {
        ASSERT_EQ(stage.spare,
                  sim_find_next_control(fixture.server_world(),
                                        static_cast<short>(bound_team)));
    });

    fixture.with_server_context([&] { stage.hero0->set_dead(1); });
    const std::uint32_t tick_before = fixture.server_world().tick_count_;
    fixture.step_ticks(6);
    EXPECT_EQ(tick_before + 6, fixture.server_world().tick_count_)
        << "a Follow seat must keep the authoritative tick running";
    EXPECT_EQ(0, fixture.server_world().ending);
    EXPECT_FALSE(fixture.server_world().game_ended);
    EXPECT_EQ(nullptr, fixture.server_control(0))
        << "owner-locked must NOT hand player 0 the foreign spare";
    EXPECT_EQ(-1, stage.spare->user())
        << "the refused hero is never stamped (its AI keeps running)";
    EXPECT_EQ(0u, fixture.client(0).controlled_entity_ids()[0])
        << "the ControlChange entity 0 must reach the mirror";
    EXPECT_EQ(stage.hero1, fixture.server_control(1))
        << "the owning player's claimed seat is untouched";
    EXPECT_EQ(id1, fixture.client(0).controlled_entity_ids()[1]);
    fixture.expect_clients_match_server();

    // [NET-F3] terminal: the spare and the claimed teammate fall — the wipe
    // endgame stays reachable from the Follow state.
    fixture.with_server_context([&] {
        stage.hero1->set_dead(1);
        stage.spare->set_dead(1);
    });
    const std::uint32_t tick_at_wipe = fixture.server_world().tick_count_;
    fixture.step_ticks(4);
    EXPECT_EQ(tick_at_wipe, fixture.server_world().tick_count_)
        << "the full wipe must stop the authoritative tick";
    EXPECT_EQ(1, fixture.server_world().ending)
        << "the full wipe must request the loss endgame";
}

// §4.4 site 1 policy-ON, server end to end ("a client claiming a foreign
// walker is refused"): a SwitchChar InputMessage from the owning client
// cannot cycle onto a foreign machine's hero — the server-side conjunction
// filters it and the seat falls back to its own walker.
TEST(CtfNetwork, owner_locked_switch_char_input_cannot_claim_foreign_hero)
{
    NetworkTestFixture fixture({
        .player_count = 2,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();

    const OwnerLockedAlliedStage stage =
        stage_owner_locked_allied_pair(fixture);
    ASSERT_NE(nullptr, stage.hero0);
    ASSERT_NE(nullptr, stage.spare);
    const std::uint32_t id0 = stage.hero0->entity_id();

    fixture.initial_sync();
    fixture.step_ticks(2);
    ASSERT_EQ(stage.hero0, fixture.server_control(0));

    InputState switch_input;
    switch_input.clear();
    switch_input.players[0]
        .pressed[static_cast<int>(InputAction::SwitchChar)] = true;
    fixture.step_tick(switch_input);
    EXPECT_EQ(stage.hero0, fixture.server_control(0))
        << "the cycle must refuse the foreign hero and fall back";
    EXPECT_EQ(-1, stage.spare->user())
        << "the foreign hero is never stamped by the refused claim";

    // The release/re-stamp dance completes on the next tick: the seat's own
    // walker carries its user tag again and the mirror mapping is unchanged.
    InputState idle;
    idle.clear();
    fixture.step_tick(idle);
    EXPECT_EQ(0, stage.hero0->user());
    EXPECT_EQ(id0, fixture.client(0).controlled_entity_ids()[0]);
    fixture.expect_clients_match_server();
}

// §4.4 site 3/4 policy-ON: the bind-time claim (and the per-level rebind
// that funnels through it) gives every deployed machine its OWN hero
// regardless of oblist order, and binds a 0-deploy machine's seat null (the
// [NET-F3] follow seat) while the watched heroes keep running unstamped.
TEST(CtfNetwork, owner_locked_bind_gives_own_hero_and_zero_deploy_binds_null)
{
    NetworkTestFixture fixture({
        .player_count = 3,
        .level_id = 1,
    });
    fixture.load_level();

    walker* first = nullptr;
    walker* second = nullptr;
    fixture.with_server_context([&] {
        // NON-CTF world: this exercises the generic owner-locked claim, not
        // the CTF own-hero preference.
        for (const auto& uptr : fixture.server_world().oblist)
        {
            walker* w = uptr.get();
            if (w == nullptr || w->dead() ||
                w->query_order() != Order::Living || w->team_num() != 0)
            {
                continue;
            }
            if (first == nullptr)
            {
                first = w;
                continue;
            }
            second = w;
            break;
        }
        if (second == nullptr)
        {
            second = fixture.server_world().add_ob(Order::Living, FAMILY_SOLDIER);
            ASSERT_NE(nullptr, second);
            second->setxy(static_cast<short>(first->xpos() + 32),
                          static_cast<short>(first->ypos()));
            second->set_team_num(0);
        }

        // Tag in REVERSE order: the earlier walker belongs to player 1's
        // machine, the later one to player 0's. The legacy pool would hand
        // player 0 the earlier walker; owner-locked must not.
        first->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        first->myguy->id = 81;
        first->myguy->owner_player_index = 1;
        second->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        second->myguy->id = 82;
        second->myguy->owner_player_index = 0;

        std::array<std::uint8_t, og::sim::kPlayerMachineSlots> machines;
        machines.fill(og::sim::kPlayerMachineNone);
        machines[0] = og::sim::encode_player_machine(0, true);
        machines[1] = og::sim::encode_player_machine(1, true);
        machines[2] = og::sim::encode_player_machine(2, false);
        og::sim::set_control_policy(fixture.server_world(),
                                    og::sim::kControlPolicyOwnerLocked,
                                    machines);
    });
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);

    fixture.rebind_players();

    EXPECT_EQ(second, fixture.server_control(0))
        << "player 0 must claim its own machine's hero";
    EXPECT_EQ(first, fixture.server_control(1))
        << "player 1 must claim its own machine's hero";
    EXPECT_EQ(nullptr, fixture.server_control(2))
        << "the 0-deploy machine's seat must bind null (follow seat)";
    EXPECT_EQ(0, second->user());
    EXPECT_EQ(1, first->user());

    // The null seat reaches the mirror as entity 0; the watched heroes'
    // mappings are the owners', untouched.
    fixture.initial_sync();
    EXPECT_EQ(0u, fixture.client(2).controlled_entity_ids()[2]);
    EXPECT_EQ(second->entity_id(), fixture.client(2).controlled_entity_ids()[0]);
    EXPECT_EQ(first->entity_id(), fixture.client(2).controlled_entity_ids()[1]);
}

// §4.4 compatibility policy-ON: user tags and control assignments both
// survive death/revive under owner-locked.
TEST(CtfNetwork, owner_locked_control_assignment_survives_death_and_respawn)
{
    NetworkTestFixture fixture({
        .player_count = 1,
        .level_id = 1,
        .validate_serialization = true,
    });
    fixture.load_level();
    inject_ctf_scenario(fixture, 1, /*requested_respawn_ticks=*/8);

    walker* player = fixture.server_control(0);
    ASSERT_NE(nullptr, player);
    const std::uint32_t player_id = player->entity_id();
    fixture.with_server_context([&] {
        player->set_owned_myguy(std::make_unique<guy>(FAMILY_SOLDIER));
        player->myguy->id = 91;
        player->myguy->owner_player_index = 0;

        std::array<std::uint8_t, og::sim::kPlayerMachineSlots> machines;
        machines.fill(og::sim::kPlayerMachineNone);
        machines[0] = og::sim::encode_player_machine(0, true);
        og::sim::set_control_policy(fixture.server_world(),
                                    og::sim::kControlPolicyOwnerLocked,
                                    machines);
    });
    const unsigned char bound_team = player->team_num();
    const unsigned char enemy_team = bound_team == 0 ? 1 : 0;
    isolate_bound_players_on_team(fixture, bound_team, enemy_team, 1);

    fixture.initial_sync();
    fixture.step_ticks(2);
    ASSERT_TRUE(fixture.server_world().ctf.active);

    fixture.with_server_context([&] { player->set_dead(1); });
    fixture.step_ticks(2);
    ASSERT_EQ(player, fixture.server_control(0));
    ASSERT_EQ(player_id, fixture.client(0).controlled_entity_ids()[0]);

    fixture.step_ticks(40);
    walker* revived = fixture.server_world().find_by_id(player_id);
    ASSERT_NE(nullptr, revived);
    ASSERT_FALSE(revived->dead()) << "the sim revive must have fired";
    EXPECT_EQ(0, revived->user())
        << "the user tag survives death and revive (§4.4)";
    EXPECT_EQ(revived, fixture.server_control(0));
    EXPECT_EQ(player_id, fixture.client(0).controlled_entity_ids()[0]);
    fixture.expect_clients_match_server();
}
