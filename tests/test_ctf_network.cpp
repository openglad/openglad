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

// Playtest bug A regression: a solo-team player (no fallback body) whose
// control nulls at death MUST be rebound after the CTF revive. The revived
// walker keeps user_=player_index (revive_player_walker preserves identity),
// which no sim_find_next_control pass can claim (all require user()==-1) —
// the server's reclaim-by-user-tag scan is the only rebind path, and its
// ControlChange must drive the client mirror's mapping to the revived id.
TEST(CtfNetwork, solo_team_player_is_rebound_after_respawn)
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

    // Kill the player's walker: with no fallback body the control nulls and
    // the mirror's mapping drops to entity 0 while the respawn is pending.
    fixture.with_server_context([&] { player->set_dead(1); });
    fixture.step_ticks(2);
    ASSERT_TRUE(og::sim::ctf_pending_player_respawn(
        fixture.server_world().ctf, player_id))
        << "the death scan should schedule the player corpse";
    ASSERT_EQ(nullptr, fixture.server_control(0))
        << "no fallback body: the control must be null while dead";
    ASSERT_EQ(0u, fixture.client(0).controlled_entity_ids()[0]);

    // Far past the 8-tick respawn timer: the revive fired and the reclaim
    // must have rebound the player on both the server and the mirror.
    fixture.step_ticks(40);
    walker* revived = fixture.server_world().find_by_id(player_id);
    ASSERT_NE(nullptr, revived);
    ASSERT_FALSE(revived->dead()) << "the sim revive must have fired";
    EXPECT_EQ(0, revived->user()) << "revive preserves the player's user tag";
    EXPECT_EQ(revived, fixture.server_control(0))
        << "the server must reclaim the revived walker by user tag";
    EXPECT_EQ(player_id, fixture.client(0).controlled_entity_ids()[0])
        << "the reclaim ControlChange must rebind the client mirror";
    fixture.expect_clients_match_server();
}

// Two same-team players who both die with no fallback bodies must each
// reclaim their OWN character: user tags are identity-exact, so iteration
// order can never swap the revived walkers between the players.
TEST(CtfNetwork, two_same_team_dead_players_reclaim_their_own_characters)
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
    ASSERT_EQ(nullptr, fixture.server_control(0));
    ASSERT_EQ(nullptr, fixture.server_control(1));

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

TEST(CtfNetwork, multi_seat_peer_reclaims_every_seat_after_respawn)
{
    // Both seats of ONE peer die with no fallback bodies; the CTF revive must
    // rebind EACH seat to its own character (per-seat reclaim inside the
    // peer's bound_players loop), and the ControlChanges must land in the
    // shared client mirror's per-player mapping.
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
    ASSERT_EQ(nullptr, fixture.server_control(0))
        << "no fallback body: seat 0's control must be null while dead";
    ASSERT_EQ(nullptr, fixture.server_control(1))
        << "no fallback body: seat 1's control must be null while dead";

    fixture.step_ticks(40);
    walker* revived0 = fixture.server_world().find_by_id(id0);
    walker* revived1 = fixture.server_world().find_by_id(id1);
    ASSERT_NE(nullptr, revived0);
    ASSERT_NE(nullptr, revived1);
    ASSERT_FALSE(revived0->dead());
    ASSERT_FALSE(revived1->dead());
    EXPECT_EQ(revived0, fixture.server_control(0))
        << "seat 0 must reclaim its own character (exact user tag)";
    EXPECT_EQ(revived1, fixture.server_control(1))
        << "seat 1 must reclaim its own character (exact user tag)";
    EXPECT_EQ(0, revived0->user());
    EXPECT_EQ(1, revived1->user());
    EXPECT_EQ(id0, fixture.client(0).controlled_entity_ids()[0])
        << "seat 0's reclaim ControlChange must rebind the shared mirror";
    EXPECT_EQ(id1, fixture.client(0).controlled_entity_ids()[1])
        << "seat 1's reclaim ControlChange must rebind the shared mirror";
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
        // The networked-allied shape: the world is allied-folded onto the
        // shared bound team and both heroes are OWNED (myguy attached).
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
