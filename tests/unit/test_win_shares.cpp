// Networked win-pot money split (og::progression, win_shares.cpp): the exact
// §4.6 deploy-ratio table (floor + remainder to the largest contributor, tie
// broken to the lowest player index), conservation, the zero-deploy and
// no-contributor edges, the baseline+share wallet overlay, and the deploy
// roster capture from a world.

#include <gtest/gtest.h>

#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/save_data.h>
#include <openglad/resources/win_shares.h>

#include "test_game_world_fixture.h"

#include <array>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace {

using og::progression::NetWinFoldCapture;
using og::progression::NetWinShareTable;
using og::progression::WinShareContributor;
using og::progression::apply_networked_win_shares;
using og::progression::compute_networked_win_shares;
using og::progression::machine_cash_share;

// One contributor per (owner, team, count) triple.
void add_contributors(NetWinFoldCapture& capture, std::uint8_t owner, short team,
                      int count)
{
    for (int i = 0; i < count; ++i)
        capture.deployed.push_back(WinShareContributor{.owner = owner, .team = team});
}

std::uint64_t sum_team(const std::array<std::uint64_t, og::sim::kMaxGlobalPlayers>& row)
{
    return std::accumulate(row.begin(), row.end(), std::uint64_t{0});
}

// ---------------------------------------------------------------------------
// The canonical §4.6 example: seven single-character players all allied on
// team 0. The pot splits by floor with the remainder to the lowest player
// index (all counts tie at 1), and each machine banks the sum of its players.

TEST(WinShares, seven_player_allied_split_matches_the_design_table)
{
    NetWinFoldCapture capture;
    capture.cash_delta[0] = 250; // 250/7 = 35 floor, remainder 5
    capture.score_delta[0] = 123; // 123/7 = 17 floor, remainder 4
    for (std::uint8_t player = 0; player < 7; ++player)
        add_contributors(capture, player, 0, 1);

    const NetWinShareTable table = compute_networked_win_shares(capture);

    // Cash: player 0 gets the remainder (lowest index of the tied largest).
    EXPECT_EQ(40u, table.cash[0][0]) << "35 floor + 5 remainder";
    for (std::uint8_t player = 1; player < 7; ++player)
        EXPECT_EQ(35u, table.cash[0][player]) << "player " << int(player);
    // Score splits the same way.
    EXPECT_EQ(21u, table.score[0][0]) << "17 floor + 4 remainder";
    for (std::uint8_t player = 1; player < 7; ++player)
        EXPECT_EQ(17u, table.score[0][player]) << "player " << int(player);

    // Conservation.
    EXPECT_EQ(250u, sum_team(table.cash[0]));
    EXPECT_EQ(123u, sum_team(table.score[0]));

    // Machine banks: host{0,1}, joinerA{2,3,4,5}, joinerB{6}.
    const std::array<std::uint8_t, 2> host = {0, 1};
    const std::array<std::uint8_t, 4> join_a = {2, 3, 4, 5};
    const std::array<std::uint8_t, 1> join_b = {6};
    EXPECT_EQ(75u, machine_cash_share(capture, std::span<const std::uint8_t>(host)))
        << "40 + 35";
    EXPECT_EQ(140u,
              machine_cash_share(capture, std::span<const std::uint8_t>(join_a)))
        << "4 * 35";
    EXPECT_EQ(35u,
              machine_cash_share(capture, std::span<const std::uint8_t>(join_b)));
    // The three machine banks conserve to the pot — no duplication.
    EXPECT_EQ(250u, 75u + 140u + 35u);
}

// ---------------------------------------------------------------------------
// Remainder goes to the strictly-largest contributor.

TEST(WinShares, remainder_goes_to_the_largest_contributor)
{
    NetWinFoldCapture capture;
    capture.cash_delta[0] = 10;
    add_contributors(capture, /*owner=*/0, /*team=*/0, /*count=*/3);
    add_contributors(capture, /*owner=*/1, /*team=*/0, /*count=*/1);

    const NetWinShareTable table = compute_networked_win_shares(capture);
    // 10*3/4 = 7, 10*1/4 = 2, assigned 9, remainder 1 -> player 0 (count 3).
    EXPECT_EQ(8u, table.cash[0][0]);
    EXPECT_EQ(2u, table.cash[0][1]);
    EXPECT_EQ(10u, sum_team(table.cash[0]));
}

// A count tie sends the remainder to the lowest player index.

TEST(WinShares, remainder_tie_breaks_to_the_lowest_player_index)
{
    NetWinFoldCapture capture;
    capture.cash_delta[0] = 11;
    add_contributors(capture, /*owner=*/0, /*team=*/0, /*count=*/2);
    add_contributors(capture, /*owner=*/2, /*team=*/0, /*count=*/2);

    const NetWinShareTable table = compute_networked_win_shares(capture);
    // 11*2/4 = 5 each, assigned 10, remainder 1 -> lowest tied index (0).
    EXPECT_EQ(6u, table.cash[0][0]);
    EXPECT_EQ(5u, table.cash[0][2]);
    EXPECT_EQ(11u, sum_team(table.cash[0]));
}

// ---------------------------------------------------------------------------
// A team whose money delta has no deployed contributor leaves the delta
// undistributed (no crash, no phantom credit).

TEST(WinShares, delta_with_no_contributor_is_undistributed)
{
    NetWinFoldCapture capture;
    capture.cash_delta[1] = 100; // team 1 earned, but nobody deployed there
    add_contributors(capture, /*owner=*/0, /*team=*/0, /*count=*/1);
    capture.cash_delta[0] = 40;

    const NetWinShareTable table = compute_networked_win_shares(capture);
    EXPECT_EQ(0u, sum_team(table.cash[1])) << "team 1 delta cannot be assigned";
    EXPECT_EQ(40u, table.cash[0][0]);
}

// Independent per-team splits.

TEST(WinShares, teams_split_independently)
{
    NetWinFoldCapture capture;
    capture.cash_delta[0] = 30;
    capture.cash_delta[3] = 9;
    add_contributors(capture, /*owner=*/0, /*team=*/0, /*count=*/1);
    add_contributors(capture, /*owner=*/1, /*team=*/0, /*count=*/1);
    add_contributors(capture, /*owner=*/2, /*team=*/3, /*count=*/1);

    const NetWinShareTable table = compute_networked_win_shares(capture);
    EXPECT_EQ(15u, table.cash[0][0]);
    EXPECT_EQ(15u, table.cash[0][1]);
    EXPECT_EQ(9u, table.cash[3][2]);
    EXPECT_EQ(0u, table.cash[3][0]);
}

// ---------------------------------------------------------------------------
// apply_networked_win_shares: baseline + share, legacy scalar mirror, and the
// §4.7 deployed count.

TEST(WinShares, apply_overlays_baseline_plus_share_and_reports_deploy_count)
{
    NetWinFoldCapture capture;
    capture.cash_delta[0] = 250;
    capture.score_delta[0] = 123;
    for (std::uint8_t player = 0; player < 7; ++player)
        add_contributors(capture, player, 0, 1);

    SaveData save;
    save.reset();
    for (std::size_t t = 0; t < 4; ++t)
    {
        save.m_totalcash[t] = 1000u + static_cast<std::uint32_t>(t);
        save.m_totalscore[t] = 2000u + static_cast<std::uint32_t>(t);
    }

    const std::array<std::uint8_t, 2> host = {0, 1};
    const int deployed = apply_networked_win_shares(
        save, capture, std::span<const std::uint8_t>(host),
        std::optional<std::size_t>(0));

    EXPECT_EQ(2, deployed) << "host owns 2 deployed characters";
    EXPECT_EQ(1000u + 75u, save.m_totalcash[0]) << "baseline + host share";
    EXPECT_EQ(2000u + 38u, save.m_totalscore[0]) << "score share 21 + 17";
    // Untouched teams stay at their disk baseline.
    EXPECT_EQ(1001u, save.m_totalcash[1]);
    EXPECT_EQ(2003u, save.m_totalscore[3]);
    // Legacy scalar mirrors follow the primary team's merged totals.
    EXPECT_EQ(save.m_totalcash[0], save.totalcash);
    EXPECT_EQ(save.m_totalscore[0], save.totalscore);
}

TEST(WinShares, apply_for_a_zero_deploy_machine_banks_nothing)
{
    NetWinFoldCapture capture;
    capture.cash_delta[0] = 250;
    for (std::uint8_t player = 0; player < 7; ++player)
        add_contributors(capture, player, 0, 1);

    SaveData save;
    save.reset();
    save.m_totalcash[0] = 5000u;

    // Player 9 deployed nobody in this capture.
    const std::array<std::uint8_t, 1> spectator = {9};
    const int deployed = apply_networked_win_shares(
        save, capture, std::span<const std::uint8_t>(spectator), std::nullopt);

    EXPECT_EQ(0, deployed);
    EXPECT_EQ(5000u, save.m_totalcash[0]) << "no share for a 0-deploy machine";
}

// ---------------------------------------------------------------------------
// collect_deployed_contributors reads owner + in-level team off the world's
// player-owned walkers (dead ones still count — deploy-time roster).

TEST(WinShares, collect_reads_owner_and_team_from_owned_walkers)
{
    TestGameWorld fx;
    GameWorld& world = fx.world();

    const auto add_owned = [&](unsigned char family, std::uint8_t owner,
                               unsigned char team, bool dead) {
        walker* const w = world.add_ob(Order::Living, family);
        ASSERT_NE(nullptr, w);
        w->set_owned_myguy(std::make_unique<guy>(family));
        w->myguy->owner_player_index = owner;
        w->set_team_num(team);
        w->set_dead(dead ? 1 : 0);
    };
    add_owned(FAMILY_SOLDIER, /*owner=*/0, /*team=*/0, /*dead=*/false);
    add_owned(FAMILY_ARCHER, /*owner=*/1, /*team=*/0, /*dead=*/true); // still counts
    // An unowned scenario troop (no myguy) is skipped.
    walker* troop = world.add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, troop);
    troop->set_team_num(0);

    const std::vector<WinShareContributor> contributors =
        og::progression::collect_deployed_contributors(world);

    ASSERT_EQ(2u, contributors.size()) << "only owned walkers, dead included";
    EXPECT_EQ(0, contributors[0].owner);
    EXPECT_EQ(1, contributors[1].owner);
    EXPECT_EQ(0, contributors[0].team);
}

} // namespace
