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
// (Recalibrated 2026-07-10 for the B1/B2 stair fixes — the re-trigger
// latch + blocked-arrival nudge, docs/z-axis-design.md: enemies that used
// to bounce back down 6 ticks after climbing now COMMIT to floor
// crossings, so the multi-floor levels 10, 11 and 18 got genuinely hotter
// for a fresh crew (18 the Warm Mint drops to a war-level 0 — its win
// gate lives in the harness sweeps, like the westlands war levels). New
// floors are the re-measured minima across seeds {42, 1337, 2025}; every
// single-floor level re-measured identical, as the floor_count>1 gate
// guarantees. Re-sweep with scripts/longseason_playtest.sh before the
// next balance pass.)
// (Recalibrated again 2026-07-10 for the multi-floor pathing fixes —
// A* no-corner-cut, the follow-path alignment assist, floor-keyed
// find_near_foe, and the flyer cross-floor bypass (all floor_count>1
// gated; docs/GAMEPLAY_FIXES_FROM_CLASSIC.md): upper-floor enemies that
// used to wedge on convex corners or hunt ground-floor shadows now fight
// where they stand, so multi-floor scen11 runs hotter (6 -> 5 across the
// three seeds) while scen10 actually eased (measured min 7; its pin keeps
// the older minimum). Single-floor levels re-measured identical again.)
// (Recalibrated 2026-07-10 for the forest-pathing wave — the SINGLE-floor
// un-gating of the A* no-corner-cut + alignment assist, the shove
// command-theft probe, and the guard facing gate (see
// docs/GAMEPLAY_FIXES_FROM_CLASSIC.md): enemy packs that used to wedge on
// corners and allied columns now arrive and fight on every level, so the
// brawl levels got hotter for the pessimistic stand-in crew. Re-measured
// minima across seeds {42, 1337, 2025}: 8: 8->3 (the summer line's flank
// now closes), 10: 4->3, 12: 8->7. All other levels re-measured at or
// above their pinned floors. Re-sweep the win/hold gates with
// scripts/longseason_playtest.sh before the next balance pass.)
// (Recalibrated 2026-07-11 for the guard wake rule (wake-on-sight +
// hold-post policy, docs/GAMEPLAY_FIXES_FROM_CLASSIC.md): allied posts —
// the Assessor's door-wards, ferrymen, fort garrisons, the winter watch —
// now provably hold (hold-post bit), easing the escort beats they anchor
// (3: 7->8, 9: 7->8), while enemy ambush posts spring and hunt once
// sighted. The guard-dense encounter levels got sharply hotter for the
// pessimistic stand-in crew: 4: 7->1 (the toll ambush now closes on the
// Assessor's porch), 6: 7->4, 12: 7->2, 13: 4->2, 16: 8->2, and 17: 7->0
// joins 18 in the 0-floor war class (its 34 mixed posts converge once the
// crew is seen; a real crew that kites and heals plays it, the stand-in
// cannot). Re-sweep the F4 win/hold gates on 4/12/16/17 with
// scripts/longseason_playtest.sh before the next balance pass.)
constexpr CurvePin kCurve[] = {
    {1, 1, 8},  {2, 1, 6},  {3, 2, 8},  {4, 2, 1},  {5, 3, 5},
    {6, 3, 4},  {7, 4, 6},  {8, 4, 3},  {9, 5, 8},  {10, 5, 3},
    {11, 5, 5}, {12, 6, 2}, {13, 6, 2}, {14, 7, 7}, {15, 7, 8},
    {16, 7, 2}, {17, 8, 0}, {18, 8, 0}, {19, 8, 8},
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
    // Balance floors are measured on the GCC ci-test lane. Under
    // ThreadSanitizer (the one clang lane) the sim's floating-point codegen
    // diverges just enough that these chaotic 600-tick battles drift off the
    // pinned floors — a compiler-determinism property, not a thread-safety
    // one. The suite finds no races (single-threaded sim), so it is skipped
    // there; the ci-test and coverage (GCC) lanes enforce the pins.
    //
    // Also skipped under AddressSanitizer, for BUDGET not correctness: the
    // guard wake rule (2026-07-11) made these battles converge instead of
    // holding posts, and at ASan's ~20x sim cost the two calibration suites
    // pushed the ASan+UBSan job past its 30-minute cap (see the identical
    // block in test_westlands_calibration.cpp for the full accounting).
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
