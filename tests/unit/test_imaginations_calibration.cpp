/* Fresh-team battle sanity for the Imaginations campaign
 * (campaigns/imaginations/README.md, "Difficulty curve").
 *
 * Every dream-log level must be clearable by a brand-new team, so the
 * curve is anchored at crew 1. The full kill-all gate (level_done within
 * 6000 ticks, bracket sweeps at crew {1, 2} x 3 seeds x 2 rosters) runs in
 * the playtest harness (scripts/imaginations_playtest.sh). Here the 8-unit
 * mixed fresh team at crew 1 is deployed exactly the way the harness
 * deploys it and the sim runs 600 ticks on seed 42; the test asserts that
 * the fight happened: the level loads and deploys the whole crew, it
 * fields an army against it, the crew takes damage and the crew deals
 * damage.
 *
 * Survivor counts are no longer asserted (2026-11-02). Until then every row
 * carried a survival floor measured on seeds {42, 1337, 2025} and asserted
 * on seed 42. These brawls are chaotic: on the three Westlands rows
 * measured over a hundred seeds the unchanged game missed its own band on 5
 * to 28 of them, so the four campaign calibration suites went red on nearly
 * every simulation change and were re-pinned eight times. The pins were a
 * seed lottery, not a contract, and the maintainer deleted them once the
 * semantic tests in tests/unit/test_sem_*.cpp asserted every game rule
 * directly.
 *
 * Measurement mode: set IMAGINATIONS_CALIBRATION_MEASURE=1 to print the
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

#include <cstdio>
#include <cstdlib>

namespace {

using westlands_fixture::MountedCampaignTest;
using westlands_fixture::CurveBattle;

class ImaginationsCalibration : public MountedCampaignTest
{
protected:
    ImaginationsCalibration()
        : MountedCampaignTest("imaginations")
    {
    }
};

struct CurveLevel
{
    int level_id;
    int crew_level; // campaign README curve ("crew power entering")
};

// The Raspberry Isle, the Castle of Gold and the Asteroid Castle, all at
// the fresh-team anchor. By tick 600 the isle's garrison has fought the
// landing, the Castle of Gold's four tick-0 faeries are awake (its waves
// wake at 400/900/1500/2200) and the Asteroid Castle's first gust (tick
// 300) has met the spikes and the gate.
constexpr CurveLevel kCurve[] = {
    {1, 1},
    {2, 1},
    {3, 1},
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

TEST_F(ImaginationsCalibration,
       curve_levels_load_field_an_army_and_fight_at_600_ticks)
{
    // Skipped under ThreadSanitizer and AddressSanitizer for job budget,
    // like the Westlands suite (see the identical block in
    // test_westlands_calibration.cpp for the accounting): the battle sim is
    // single-threaded, and the ci-test and coverage lanes run this test.
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "campaign battle sanity runs on the ci-test/coverage "
                    "lanes; skipped under TSan and ASan (job budget)";
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    GTEST_SKIP() << "campaign battle sanity runs on the ci-test/coverage "
                    "lanes; skipped under TSan and ASan (job budget)";
#endif
#endif

    const bool measure =
        std::getenv("IMAGINATIONS_CALIBRATION_MEASURE") != nullptr;

    for (const CurveLevel& lvl : kCurve)
    {
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
