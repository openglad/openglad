/* Curve-level battle sanity for The Long Season
 * (campaigns/longseason/README.md, "Campaign metadata" >
 * "F4 CALIBRATION TABLE").
 *
 * Per level, the 8-unit mixed fresh team (4 soldiers, elf, archer, cleric,
 * barbarian, the playtest harness's representative full-lobby roster) is
 * deployed at the campaign_meta difficulty-curve crew level exactly the way
 * the harness deploys it (the campaign is anchored at crew 1: levels 1-2
 * run a brand-new team), and the sim runs 600 ticks on seed 42. The test
 * asserts that the fight happened: the level loads and deploys the whole
 * crew, it fields an army against it, the crew takes damage and the crew
 * deals damage.
 *
 * Survivor counts are no longer asserted (2026-11-02). Until then every row
 * carried a survival floor measured on seeds {42, 1337, 2025} and asserted
 * on seed 42, and a second test held Ashfall Fair's team 0 at four or more
 * standing at tick 3000 on those three seeds. These brawls are chaotic: on
 * the three Westlands rows measured over a hundred seeds the unchanged game
 * missed its own band on 5 to 28 of them, so the four campaign calibration
 * suites went red on nearly every simulation change and were re-pinned
 * eight times. The pins were a seed lottery, not a contract, and the
 * maintainer deleted them once the semantic tests in
 * tests/unit/test_sem_*.cpp asserted every game rule directly. Campaign
 * balance is the playtest harness's business
 * (scripts/longseason_playtest.sh).
 *
 * Measurement mode: set LONGSEASON_CALIBRATION_MEASURE=1 to print the
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

using westlands_fixture::MountedCampaignTest;
using westlands_fixture::CurveBattle;

class LongSeasonCalibration : public MountedCampaignTest
{
protected:
    LongSeasonCalibration()
        : MountedCampaignTest("longseason")
    {
    }
};

struct CurveLevel
{
    int level_id;
    int crew_level; // campaign_meta curve ("crew power entering")
};

// The difficulty-curve contract (campaign_meta.md, F4 calibration table):
// each level and the crew level a fresh team enters it at.
constexpr CurveLevel kCurve[] = {
    {1, 1},  {2, 1},  {3, 2},  {4, 2},  {5, 3},  {6, 3},  {7, 4},
    {8, 4},  {9, 5},  {10, 5}, {11, 5}, {12, 6}, {13, 6}, {14, 7},
    {15, 7}, {16, 7}, {17, 8}, {18, 8}, {19, 8},
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
namespace {
void check_curve_partition(std::size_t partition)
{
    const bool measure =
        std::getenv("LONGSEASON_CALIBRATION_MEASURE") != nullptr;

    for (std::size_t index = partition; index < std::size(kCurve); index += 4)
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

TEST_F(LongSeasonCalibration, curve_levels_load_field_an_army_and_fight_at_600_ticks)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(0));
}

TEST_F(LongSeasonCalibration, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_1)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(1));
}

TEST_F(LongSeasonCalibration, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_2)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(2));
}

TEST_F(LongSeasonCalibration, curve_levels_load_field_an_army_and_fight_at_600_ticks_part_3)
{
    ASSERT_NO_FATAL_FAILURE(check_curve_partition(3));
}
