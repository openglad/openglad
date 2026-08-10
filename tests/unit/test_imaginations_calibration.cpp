/* Fresh-team calibration pin for the Imaginations campaign
 * (campaigns/imaginations/README.md, "Difficulty curve").
 *
 * The campaign contract: every dream-log level must be clearable by a
 * brand-new team — the curve is ANCHORED at crew 1 for every level until
 * a submitted idea demands otherwise. The full kill-all gate (level_done
 * within 6000 ticks, bracket sweeps at crew {1, 2} x 3 seeds x 2
 * rosters) runs in the playtest harness
 * (scripts/imaginations_playtest.sh); CI pins the CHEAP invariant: the
 * 8-unit mixed fresh team at crew 1, deployed exactly the way the
 * harness deploys it, still has at least `floor` members alive at tick
 * 600 on the pinned seed. Floors are the MINIMUM measured across seeds
 * {42, 1337, 2025} at calibration time; the test runs seed 42 only.
 *
 * Measurement mode: set IMAGINATIONS_CALIBRATION_MEASURE=1 to print the
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

class ImaginationsCalibration : public MountedCampaignTest
{
protected:
    ImaginationsCalibration()
        : MountedCampaignTest("imaginations")
    {
    }
};

struct CurvePin
{
    int level_id;
    int crew_level; // campaign README curve ("crew power entering")
    int floor;      // min 8-mixed-crew survivors at tick 600 across 3 seeds
};

// The Raspberry Isle at the fresh-team anchor. The measured floor is high
// on purpose: the garrison holds its posts behind the moat until the crew
// closes, so a scattered fresh landing bleeds little in the first 600
// ticks — accessibility for new teams IS the level's design contract.
constexpr CurvePin kCurve[] = {
    {1, 1, 8},
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

TEST_F(ImaginationsCalibration, curve_crew_survival_floors_at_600_ticks)
{
    // Balance floors are measured on the GCC ci-test lane. Under
    // ThreadSanitizer (the one clang lane) the sim's floating-point codegen
    // diverges just enough that chaotic 600-tick battles drift off the
    // pinned floors — a compiler-determinism property, not a thread-safety
    // one. Skipped under AddressSanitizer for BUDGET, not correctness (see
    // the identical block in test_westlands_calibration.cpp for the full
    // accounting).
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "balance pins are enforced on the ci-test/coverage lanes; "
                    "skipped under TSan (float drift) and ASan (job budget)";
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    GTEST_SKIP() << "balance pins are enforced on the ci-test/coverage lanes; "
                    "skipped under TSan (float drift) and ASan (job budget)";
#endif
#endif

    const bool measure =
        std::getenv("IMAGINATIONS_CALIBRATION_MEASURE") != nullptr;

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
            << "a fresh crew fell below its survival floor — the isle got "
               "meaningfully hotter; re-run the bracket sweeps before "
               "touching this pin";
    }
}
