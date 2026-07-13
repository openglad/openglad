/* Tower Climb calibration pins (WP-5, spec §5.11 — the CI pin; the deeper
 * pre-ship bracket sweeps are WP-7's, run with the playtest harness).
 *
 * Mechanics cloned from test_westlands_calibration.cpp: generate floors
 * {1, 5, 10, 15, 20} for the pinned run seeds via tower_floor_gen, load
 * them headlessly, deploy the 8-unit mixed fresh crew at the player-curve
 * level crew(f) = 1 + f/4, tick 600, and pin the survivor FLOOR (minimum
 * across the three pinned run seeds — everything is deterministic, so the
 * test asserts every seed against the min-pin rather than sampling one).
 *
 * The tower contract these pins guard: the ramp L(f) = 1 + (f-1)/3 vs
 * crew(f) = 1 + f/4 widens with depth — early floors are safely clearable
 * by a fresh crew, mid floors bite, and the deficit only grows (the
 * "every run ends" design). A failure here means a generator or engine
 * change made some band MEANINGFULLY hotter/colder — re-run the sweeps and
 * re-calibrate deliberately instead of bumping the number.
 *
 * Measurement mode: set TOWER_CALIBRATION_MEASURE=1 to print per-seed
 * survivor counts instead of asserting.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <openglad/core/tower_constants.h>
#include <openglad/resources/level_file_io.h>
#include <openglad/resources/mapgen/tower_floor_gen.h>

#include "westlands_sim_fixture.h"

#include <cstdio>
#include <cstdlib>

namespace {

using westlands_fixture::deploy_crew;
using westlands_fixture::LoadedWestlandsLevel;

constexpr std::uint32_t kRunSeeds[] = {42u, 1337u, 2025u};
constexpr int kCalibrationTicks = 600;

struct FloorPin
{
    int floor_number;
    int crew_level; // the player curve crew(f) = 1 + f/4
    int survivors_floor; // min 8-mixed-crew survivors at tick 600, all seeds
};

// Measured 2026-07-13 on the first shipped generator (min across run seeds
// {42, 1337, 2025}, sim seed 42, 8-mixed crew at curve):
//   f1  cl1: 7 7 7   f5  cl2: 6 6 7   f10 cl3: 2 0 0
//   f15 cl4: 3 1 2   f20 cl6: 0 0 0
// (Recalibrated 2026-07-13 for the v10 loader obmap fix: loaded
// upper-story objects used to be bucketed in the FLOOR-0 collision piles
// (setxy ran before set_floor), so every enemy the generator authored on a
// story above ground was a collision ghost — unhittable AND largely unable
// to engage. With the fix they fight for real, and the multi-story floors
// got meaningfully hotter for the pessimistic stand-in: f5 6->1 (spire
// balconies now shoot back; seed spread 1/4/8), f15 1->0 joins the boss
// floors in the 0-floor class. Re-measured: f1 7 7 7, f5 1 4 8,
// f10 0 0 0, f15 0 0 1, f20 0 0 0. Re-run WP-7's bracket sweeps before
// the next balance pass — the band-4/5 curve gates were measured under
// ghost physics.)
// The early Bailey floors keep most of the crew standing; the Undercroft
// boss floor (f10) and the Spires boss floor (f20) already wipe the
// PESSIMISTIC stand-in crew (no kiting, no healing discipline, no shop
// gold) — the same 0-floor class as the Westlands war levels, where the
// stand-in's own survival is not the contract. The monotone decline is the
// §5.11 "wipe rate monotone in f" shape; the real clearability gates live
// in WP-7's openglad_text bracket sweeps, not here. Pins are the measured
// minima — re-measure with TOWER_CALIBRATION_MEASURE=1 before touching.
constexpr FloorPin kPins[] = {
    {1, 1, 7}, {5, 2, 1}, {10, 3, 0}, {15, 4, 0}, {20, 6, 0},
};

void prune_all_floors()
{
    for (int id = og::kTowerFirstFloorLevel; id <= 760; ++id)
        (void)og::data::delete_tower_floor_files(id);
}

int survivors_at_600(std::uint32_t run_seed, int floor_number, int crew_level)
{
    prune_all_floors();
    if (!og::tower::generate_tower_floor_to_user_dir(run_seed, floor_number)
             .written)
        return -1;
    LoadedWestlandsLevel fx(og::kTowerGateLevel + floor_number, 42u);
    if (!fx.loaded)
        return -1;
    GameWorld& world = fx.world();
    std::vector<walker*> crew = deploy_crew(
        fx.level, world,
        {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
         FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN},
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

TEST(TowerCalibration, curve_crew_survival_floors_at_600_ticks)
{
    // Balance floors are measured on the GCC ci-test lane; skipped under
    // TSan (float drift off the pinned chaos) and ASan (job budget) exactly
    // like the westlands calibration suite.
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "balance pins are enforced on the ci-test/coverage lanes; "
                    "skipped under TSan (float drift) and ASan (job budget)";
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    GTEST_SKIP() << "balance pins are enforced on the ci-test/coverage lanes; "
                    "skipped under TSan (float drift) and ASan (job budget)";
#endif
#endif

    const bool measure = std::getenv("TOWER_CALIBRATION_MEASURE") != nullptr;

    for (const FloorPin& pin : kPins)
    {
        SCOPED_TRACE("floor " + std::to_string(pin.floor_number) +
                     " at crew level " + std::to_string(pin.crew_level));
        if (measure)
        {
            std::printf("TOWER CALIB floor=%d cl=%d survivors@600:",
                        pin.floor_number, pin.crew_level);
            for (std::uint32_t seed : kRunSeeds)
                std::printf(" %d", survivors_at_600(seed, pin.floor_number,
                                                    pin.crew_level));
            std::printf("\n");
            std::fflush(stdout);
            continue;
        }
        for (std::uint32_t seed : kRunSeeds)
        {
            const int alive =
                survivors_at_600(seed, pin.floor_number, pin.crew_level);
            ASSERT_GE(alive, 0) << "floor failed to generate/load/deploy "
                                   "(run seed " << seed << ")";
            EXPECT_GE(alive, pin.survivors_floor)
                << "run seed " << seed
                << ": a curve-level fresh crew fell below its survival "
                   "floor — the band got meaningfully hotter; re-measure "
                   "with TOWER_CALIBRATION_MEASURE=1 and recalibrate "
                   "deliberately";
        }
    }
    prune_all_floors();
}
