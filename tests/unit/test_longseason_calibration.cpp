/* F4 fresh-team calibration pins for The Long Season
 * (docs/campaigns/longseason/campaign_meta.md, "F4 calibration table").
 *
 * The campaign contract: a fresh team at the campaign_meta difficulty-curve
 * crew power can reasonably attempt every level, with the campaign ANCHORED
 * at crew 1 (levels 1-2 gate on a brand-new team). The full contract was
 * swept with the playtest harness (scripts/longseason_playtest.sh, crew
 * brackets {curve-1, curve, curve+1} x 3 seeds x 2 rosters, 6000-tick
 * runs); CI cannot afford that, so this test pins the CHEAP invariant per
 * level: the 8-unit mixed fresh team (4 soldiers, elf, archer, cleric,
 * barbarian — the harness's representative full-lobby roster; the
 * 4-soldier stand-in is a pessimistic floor) at the curve level, deployed
 * exactly the way the harness deploys it, still has at least `floor`
 * members alive at tick 600 on the pinned seed. Since the text client's
 * context-before-load fix, this fixture and openglad_text --protocol
 * produce bit-identical simulations, so these floors hold for the harness
 * verbatim.
 *
 * Floors are the MINIMUM measured across seeds {42, 1337, 2025} at the
 * time of calibration (the test runs seed 42 only; the min-across-seeds
 * floor absorbs future engine-side drift without going stale on the first
 * cosmetic change — and without dead slack: every floor is an actually
 * measured minimum, not a guess). A failure here means a builder or
 * engine change made some level MEANINGFULLY hotter for a fresh team —
 * re-run the F4 bracket sweeps and re-calibrate deliberately instead of
 * bumping the number.
 *
 * Measurement mode: set LONGSEASON_CALIBRATION_MEASURE=1 to print the
 * per-seed survivor counts for all three seeds instead of asserting.
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

using westlands_fixture::LoadedWestlandsLevel;
using westlands_fixture::MountedCampaignTest;
using westlands_fixture::deploy_crew;

class LongSeasonCalibration : public MountedCampaignTest
{
protected:
    LongSeasonCalibration()
        : MountedCampaignTest("org.openglad.longseason")
    {
    }
};

struct CurvePin
{
    int level_id;
    int crew_level; // campaign_meta curve ("crew power entering")
    int floor;      // min 8-mixed-crew survivors at tick 600 across 3 seeds
};

// The difficulty-curve contract (campaign_meta.md, F4 calibration table)
// with the measured 600-tick survival floors. The crew-1 anchor is real:
// levels 1 and 2 run at crew level 1, the fresh-team start. Floors of 1-5
// mark the brawl levels (the ferry hold's boat flank, the summer line
// fights, the Undermill, the wagon run, and above all the Warm Mint's
// three-way gatehall) where the stand-in crew bleeds by design — their
// win/hold gates live in the harness sweeps, not here.
constexpr CurvePin kCurve[] = {
    {1, 1, 8},  {2, 1, 6},  {3, 2, 7},  {4, 2, 7},  {5, 3, 5},
    {6, 3, 7},  {7, 4, 6},  {8, 4, 8},  {9, 5, 7},  {10, 5, 5},
    {11, 5, 7}, {12, 6, 8}, {13, 6, 4}, {14, 7, 7}, {15, 7, 8},
    {16, 7, 8}, {17, 8, 7}, {18, 8, 1}, {19, 8, 8},
};

constexpr int kCalibrationTicks = 600;

int survivors_at_600(int level_id, int crew_level, std::uint32_t seed)
{
    LoadedWestlandsLevel fx(level_id, seed);
    if (!fx.loaded)
        return -1;
    GameWorld& world = fx.world();
    std::vector<walker*> crew = deploy_crew(
        fx.level, world, {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
                          FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER,
                          FAMILY_CLERIC, FAMILY_BARBARIAN},
        crew_level);
    if (crew.size() != 8u)
        return -1;
    for (int t = 0; t < kCalibrationTicks; ++t)
        world.tick();
    int alive = 0;
    for (walker* w : crew)
        if (w != nullptr && !w->dead())
            ++alive;
    return alive;
}

} // namespace

TEST_F(LongSeasonCalibration, curve_crew_survival_floors_at_600_ticks)
{
    const bool measure =
        std::getenv("LONGSEASON_CALIBRATION_MEASURE") != nullptr;

    for (const CurvePin& pin : kCurve)
    {
        SCOPED_TRACE("scen" + std::to_string(pin.level_id) + " at crew level " +
                     std::to_string(pin.crew_level));
        if (measure)
        {
            std::printf("CALIB level=%d cl=%d survivors@600:", pin.level_id,
                        pin.crew_level);
            for (std::uint32_t seed : {42u, 1337u, 2025u})
                std::printf(" %d", survivors_at_600(pin.level_id,
                                                    pin.crew_level, seed));
            std::printf("\n");
            std::fflush(stdout);
            continue;
        }
        const int alive = survivors_at_600(pin.level_id, pin.crew_level, 42u);
        ASSERT_GE(alive, 0) << "level failed to load or deploy";
        EXPECT_GE(alive, pin.floor)
            << "a curve-level fresh crew fell below its F4 survival floor — "
               "the level got meaningfully hotter; re-run the F4 bracket "
               "sweeps before touching this pin";
    }
}
