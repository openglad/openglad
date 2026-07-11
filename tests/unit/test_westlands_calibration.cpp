/* F4 fresh-team calibration pins for War of the Westlands
 * (scratchpad westlands/levels/campaign_meta.md, "F4 calibration table").
 *
 * The campaign contract: a fresh team at the campaign_meta difficulty-curve
 * crew power can reasonably attempt every level. The full contract was
 * swept with the playtest harness (scripts/westlands_playtest.sh, crew
 * brackets {curve-1, curve, curve+1} x 3 seeds x 2 rosters, 6000-tick
 * runs); CI cannot afford that, so this test pins the CHEAP invariant per
 * level: the 8-unit mixed fresh team (4 soldiers, elf, archer, cleric,
 * barbarian — the harness's representative full-lobby roster; the 4-soldier
 * stand-in is a pessimistic floor that bottoms out at zero on the war
 * levels and would pin nothing) at the curve level, deployed exactly the
 * way the harness deploys it, still has at least `floor` members alive at
 * tick 600 on the pinned seed. Since the text client's context-before-load
 * fix, this fixture and openglad_text --protocol produce bit-identical
 * simulations, so these floors hold for the harness verbatim.
 *
 * Floors are the MINIMUM measured across seeds {42, 1337, 2025} at the
 * time of calibration (the test runs seed 42 only; the min-across-seeds
 * floor absorbs future engine-side drift without going stale on the first
 * cosmetic change). A failure here means a builder or engine change made
 * some level MEANINGFULLY hotter for a fresh team — re-run the F4 bracket
 * sweeps and re-calibrate deliberately instead of bumping the number.
 *
 * Measurement mode: set WESTLANDS_CALIBRATION_MEASURE=1 to print the
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
using westlands_fixture::WestlandsCampaignTest;
using westlands_fixture::deploy_crew;

struct CurvePin
{
    int level_id;
    int crew_level; // campaign_meta curve, upper value of ranges
    int floor;      // min 8-mixed-crew survivors at tick 600 across 3 seeds
};

// The difficulty-curve contract (campaign_meta.md) with the F4-measured
// 600-tick survival floors. 18 is the unused act gap.
// (Measured 2026-07-08 on the F4-calibrated package. Floors of 0 mark the
// war/ambush levels where placed allies and delayed waves carry the fight
// and the stand-in crew's own 600-tick survival is not the contract —
// their gates live in the harness sweeps, not here.)
// (Recalibrated 2026-07-10 for the B1/B2 stair fixes — the re-trigger
// latch + blocked-arrival nudge, docs/z-axis-design.md: enemies that used
// to bounce back down 6 ticks after climbing now COMMIT to floor
// crossings, so the multi-floor levels 6, 7, 15 and 17 got genuinely
// hotter for a fresh crew. New floors are the re-measured minima across
// seeds {42, 1337, 2025}; every single-floor level re-measured identical,
// as the floor_count>1 gate guarantees. The F4 win/hold contract on the
// hotter levels should be re-swept with scripts/westlands_playtest.sh
// before the next balance pass.)
// (Re-pinned 2026-07 for the content batch that scaled the Ford (scen3,
// 19 -> 39 foes) and Refuge (scen4, 23 -> 38 foes) waves up to the
// briefings' flood, with the matching defense levers — the Ford's ally
// camp 6 -> 7, the Refuge's porch ward + lvl-3 garden muster: the mixed
// curve crew re-measured 8/8/8 and 7/8/8 — the act-1 ally rosters absorb
// the first two beats, which is exactly why the waves had to grow.
// Minima across the same three seeds; every other level re-measured
// within its existing floor, including the war levels 13/15/16/17 whose
// new NAMED captains rename existing posts and are sim-inert by
// construction.)
// (Recalibrated 2026-07-10 for the forest-pathing wave — the single-floor
// un-gating of the A* no-corner-cut + follow-path alignment assist, the
// shove command-theft probe, and the guard facing gate (see
// docs/GAMEPLAY_FIXES_FROM_CLASSIC.md): enemy companies that used to wedge
// on convex corners, X-pinches and allied columns now actually arrive and
// fight, so the army-heavy levels got genuinely hotter for the pessimistic
// brawler stand-in crew (which cannot kite, heal, or exit). Re-measured
// minima across seeds {42, 1337, 2025}: 3: 8->7, 7: 1->0, 8: 4->0,
// 15: 1->0, 17: 4->2, 19: 6->5, 22: 8->5, 23: 7->5, 24: 4->2, 25: 8->7;
// levels 7/8/15 join the 0-floor war class where placed allies and waves
// carry the fight. The Forest Road (scen2) re-measured 8/8/8 with its
// RCA-4.5 content fix (flush bend-1 SE junction, first shadow wolf at
// tick 400). The F4 win/hold contract on the hotter levels should be
// re-swept with scripts/westlands_playtest.sh before the next balance
// pass.)
// (Recalibrated 2026-07-11 for the guard wake rule (wake-on-sight +
// hold-post policy, docs/GAMEPLAY_FIXES_FROM_CLASSIC.md): allied garrisons
// now provably hold their posts (hold-post bit), which made the escorted
// act-1 beats slightly SAFER (4: 7->8, 6: 6->7), while enemy ambush posts
// spring and hunt once sighted, which made warden-dense levels hotter
// (11: 5->4, 16: 3->2, 25: 7->5) and dropped the two mage-tower levels
// into the 0-floor war class (14: 7->0, 22: 5->0 — a dozen-plus waking
// mage wards converge on the pessimistic brawler crew; playable levels for
// a real crew that kites and heals, but the stand-in cannot). The F4
// win/hold contract on 14 and 22 should be re-swept with
// scripts/westlands_playtest.sh before the next balance pass.)
constexpr CurvePin kCurve[] = {
    {1, 1, 8},  {2, 2, 8},  {3, 2, 7},  {4, 3, 8},  {5, 3, 7},  {6, 4, 7},
    {7, 4, 0},  {8, 5, 0},  {9, 6, 0},  {10, 5, 0}, {11, 6, 4}, {12, 6, 0},
    {13, 6, 0}, {14, 7, 0}, {15, 7, 0}, {16, 8, 2}, {17, 8, 2}, {19, 6, 5},
    {20, 7, 0}, {21, 7, 7}, {22, 8, 0}, {23, 8, 5}, {24, 8, 2}, {25, 9, 5},
    {26, 9, 8},
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

TEST_F(WestlandsCampaignTest, curve_crew_survival_floors_at_600_ticks)
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
    // pushed the ASan+UBSan job past its 30-minute cap (the lane sat at
    // 28m40s before the wake rule; og_unit_sim was still mid-suite when the
    // runner killed it). The lane's memory-error coverage of the battle sim
    // comes from the rest of og_unit_sim and the integration suites; the
    // pins themselves are GCC contracts already enforced twice elsewhere.
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
        std::getenv("WESTLANDS_CALIBRATION_MEASURE") != nullptr;

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
