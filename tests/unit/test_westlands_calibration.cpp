/* Curve-level battle sanity for War of the Westlands
 * (scratchpad westlands/levels/campaign_meta.md, "F4 calibration table").
 *
 * Per level, the 8-unit mixed fresh team (4 soldiers, elf, archer, cleric,
 * barbarian, the playtest harness's representative full-lobby roster) is
 * deployed at the campaign_meta difficulty-curve crew level exactly the way
 * the harness deploys it, and the sim runs 600 ticks on seed 42. The test
 * asserts that the fight happened: the level loads and deploys the whole
 * crew, it fields an army against it, the crew takes damage and the crew
 * deals damage. A damage sink that stops landing, foes that never engage or
 * a level that no longer loads reds the row.
 *
 * Survivor counts are no longer asserted (2026-11-02). Until then every row
 * also carried a survival floor and a hot-side ceiling measured on seeds
 * {42, 1337, 2025} and asserted on seed 42. These brawls are chaotic: on
 * the three Westlands rows measured over a hundred seeds the unchanged game
 * missed its own band on 5 to 28 of them, so the four campaign calibration
 * suites went red on nearly every simulation change and were re-pinned
 * eight times. The pins were a seed lottery, not a contract, and the
 * maintainer deleted them once the semantic tests in
 * tests/unit/test_sem_*.cpp asserted every game rule directly. Campaign
 * balance is the playtest harness's business
 * (scripts/westlands_playtest.sh).
 *
 * Measurement mode: set WESTLANDS_CALIBRATION_MEASURE=1 to print the
 * per-seed survivor counts for seeds {42, 1337, 2025} and the seed-42
 * battle line (alive / host / damage taken / damage dealt) instead of
 * asserting. It is a measuring tool for balance work: it asserts nothing
 * and runs only when asked.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "westlands_sim_fixture.h"

#include <cstddef>
#include <cstdio>
#include <iterator>
#include <cstdlib>

namespace {

using westlands_fixture::WestlandsCampaignTest;
using westlands_fixture::CurveBattle;

struct CurveLevel
{
    int level_id;
    int crew_level; // campaign_meta curve, upper value of ranges
};

// The difficulty-curve contract (campaign_meta.md): each level and the
// crew level a fresh team enters it at. 18 is the unused act gap.
constexpr CurveLevel kCurve[] = {
    {1, 1},  {2, 2},  {3, 2},  {4, 3},  {5, 3},  {6, 4},  {7, 4},
    {8, 5},  {9, 6},  {10, 5}, {11, 6}, {12, 6}, {13, 6}, {14, 7},
    {15, 7}, {16, 8}, {17, 8}, {19, 6}, {20, 7}, {21, 7}, {22, 8},
    {23, 8}, {24, 8}, {25, 9}, {26, 9},
};

constexpr int kCalibrationTicks = 600;

CurveBattle battle_at_600(int level_id, int crew_level, std::uint32_t seed)
{
    // The suite plays the shipped default (both settings on);
    // OG_CALIBRATION_<NAME>=0 measures the classic value
    // (westlands_sim_fixture.h).
    return westlands_fixture::mixed_crew_battle(level_id, crew_level, seed,
                                                kCalibrationTicks);
}

} // namespace

// Each original curve row still runs once with the same seed/ticks and
// assertions. Separate cases bound instrumentation cost without a sanitizer
// skip; the original semantic test ID owns partition zero.
//
// Twelve partitions, not four: under coverage instrumentation the four-way
// split left one case at 321 s (GitHub run 43221548149, 2026-11-12), which
// put its CTest entry at 525 s of a 600 s ceiling and made it the floor of
// the whole coverage lane. The cost is two rows, not the row count: scen7
// and scen16 are about half of the suite between them (30% and 20% of an
// uninstrumented run), and a stride of twelve gives each of them a case it
// shares with one light row. The rows, seed, ticks and assertions are
// unchanged.
namespace {
constexpr std::size_t kCurvePartitions = 12;

void check_curve_partition(std::size_t partition)
{
    ASSERT_LT(partition, kCurvePartitions)
        << "a partition past the stride plays no row and passes vacuously";
    const bool measure =
        std::getenv("WESTLANDS_CALIBRATION_MEASURE") != nullptr;

    for (std::size_t index = partition; index < std::size(kCurve);
         index += kCurvePartitions)
    {
        const CurveLevel& lvl = kCurve[index];
        SCOPED_TRACE("scen" + std::to_string(lvl.level_id) + " at crew level " +
                     std::to_string(lvl.crew_level));
        if (measure)
        {
            std::printf("CALIB level=%d cl=%d survivors@600:", lvl.level_id,
                        lvl.crew_level);
            for (std::uint32_t seed : {42u, 1337u, 2025u})
                std::printf(" %d", battle_at_600(lvl.level_id,
                                                 lvl.crew_level, seed).alive);
            const CurveBattle m =
                battle_at_600(lvl.level_id, lvl.crew_level, 42u);
            std::printf("  [seed42 alive=%d host=%d taken=%.0f dealt=%.0f]\n",
                        m.alive, m.host_alive, m.crew_taken, m.crew_dealt);
            std::fflush(stdout);
            continue;
        }
        const CurveBattle r = battle_at_600(lvl.level_id, lvl.crew_level, 42u);
        ASSERT_GE(r.alive, 0) << "level failed to load or deploy the crew";
        EXPECT_GT(r.host_alive, 0) << "the level must field an army";
        EXPECT_GT(r.crew_taken, 0.0f)
            << "the crew came home untouched: the level stopped biting (a "
               "damage sink that never lands, foes that never engage)";
        EXPECT_GT(r.crew_dealt, 0.0f)
            << "the crew never landed a hit in 600 ticks: it is not fighting "
               "the level at all";
    }
}
} // namespace

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(0));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_1)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(1));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_2)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(2));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_3)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(3));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_4)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(4));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_5)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(5));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_6)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(6));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_7)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(7));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_8)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(8));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_9)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(9));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_10)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(10));
}

TEST_F(WestlandsCampaignTest, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_11)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(11));
}
