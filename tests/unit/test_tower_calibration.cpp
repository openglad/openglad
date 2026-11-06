/* Tower Climb battle sanity (WP-5, spec §5.11 — the CI check; the deeper
 * pre-ship bracket sweeps are WP-7's, run with the playtest harness).
 *
 * Mechanics cloned from test_westlands_calibration.cpp: generate floors
 * {1, 5, 10, 15, 20} for the run seeds {42, 1337, 2025} via
 * tower_floor_gen, load them headlessly, deploy the 8-unit mixed fresh crew
 * at the player-curve level crew(f) = 1 + f/4 and tick 600 on sim seed 42.
 * For every floor and run seed the test asserts that the floor generated,
 * loaded and deployed the whole crew, that it fielded at least a third of
 * the §5.6 foe count N(f), and that the fight happened: the crew took
 * damage and dealt damage.
 *
 * Survivor counts are no longer asserted (2026-11-02). Until then every
 * floor carried a survival floor and a hot-side ceiling cut from these same
 * runs. These brawls are chaotic: on the three Westlands rows measured over
 * a hundred seeds the unchanged game missed its own band on 5 to 28 of
 * them, so the four campaign calibration suites went red on nearly every
 * simulation change and were re-pinned eight times. The pins were a seed
 * lottery, not a contract, and the maintainer deleted them once the
 * semantic tests in tests/unit/test_sem_*.cpp asserted every game rule
 * directly. The tower's ramp (L(f) = 1 + (f-1)/3 against crew(f) = 1 + f/4)
 * is WP-7's bracket sweeps' business.
 *
 * Measurement mode: set TOWER_CALIBRATION_MEASURE=1 to print per-seed
 * survivor counts instead of asserting. It is a measuring tool for balance
 * work: it asserts nothing and runs only when asked.
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

struct CurveFloor
{
    int floor_number;
    int crew_level; // the player curve crew(f) = 1 + f/4
};

// What the generated floor fielded and what the 600 ticks did: an
// unpopulated deep floor (run_audits pins footing/stairs/reachability,
// never POPULATION) or foes that never bite reds the floor.
struct FloorBattle
{
    int alive = -1;          // crew standing at tick 600
    int foes = 0;            // placed team-2/3 livings the floor deploys with
    float crew_taken = 0.0f; // hit points the crew lost to the floor
    float crew_dealt = 0.0f; // hit points the crew took off the floor
};

constexpr CurveFloor kFloors[] = {
    {1, 1}, {5, 2}, {10, 3}, {15, 4}, {20, 6},
};

void prune_all_floors()
{
    for (int id = og::kTowerFirstFloorLevel; id <= 760; ++id)
        (void)og::data::delete_tower_floor_files(id);
}

// The foes the generator placed: tower posts climb on teams 2/3 (never the
// hold-post teams 0/1) — the same census test_tower_floor_gen.cpp pins.
int placed_foes(GameWorld& world)
{
    int n = 0;
    for (const auto& uptr : world.oblist)
    {
        const walker* const w = uptr.get();
        if (w != nullptr && !w->dead() &&
            w->query_order() == Order::Living &&
            (w->team_num() == 2 || w->team_num() == 3))
            ++n;
    }
    return n;
}

FloorBattle battle_at_600(std::uint32_t run_seed, int floor_number,
                          int crew_level)
{
    FloorBattle out;
    prune_all_floors();
    if (!og::tower::generate_tower_floor_to_user_dir(run_seed, floor_number)
             .written)
        return out;
    LoadedWestlandsLevel fx(og::kTowerGateLevel + floor_number, 42u);
    if (!fx.loaded)
        return out;
    GameWorld& world = fx.world();
    // The suite plays the shipped default (both on); OG_CALIBRATION_<NAME>=0
    // measures the classic value (westlands_sim_fixture.h).
    world.new_specials = westlands_fixture::calibration_setting("OG_CALIBRATION_NEW_SPECIALS");
    world.gameplay_fixes = westlands_fixture::calibration_setting("OG_CALIBRATION_GAMEPLAY_FIXES");
    std::vector<walker*> crew = deploy_crew(
        fx.level, world,
        {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
         FAMILY_ELF, FAMILY_ARCHER, FAMILY_CLERIC, FAMILY_BARBARIAN},
        crew_level);
    if (crew.size() != 8u)
        return out;
    out.foes = placed_foes(world);
    for (int t = 0; t < kCalibrationTicks; ++t)
        world.tick();
    out.alive = 0;
    for (walker* w : crew)
    {
        if (w == nullptr)
            continue;
        if (!w->dead())
            ++out.alive;
        if (w->myguy != nullptr)
        {
            out.crew_taken += w->myguy->scen_damage_taken;
            out.crew_dealt += w->myguy->scen_damage;
        }
    }
    return out;
}

int survivors_at_600(std::uint32_t run_seed, int floor_number, int crew_level)
{
    return battle_at_600(run_seed, floor_number, crew_level).alive;
}

} // namespace

TEST(TowerCalibration,
     curve_floors_generate_field_an_army_and_fight_at_600_ticks)
{
    // Skipped under ThreadSanitizer and AddressSanitizer for job budget,
    // like the Westlands suite (see the identical block in
    // test_westlands_calibration.cpp for the accounting).
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "campaign battle sanity runs on the ci-test/coverage "
                    "lanes; skipped under TSan and ASan (job budget)";
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    GTEST_SKIP() << "campaign battle sanity runs on the ci-test/coverage "
                    "lanes; skipped under TSan and ASan (job budget)";
#endif
#endif

    const bool measure = std::getenv("TOWER_CALIBRATION_MEASURE") != nullptr;

    for (const CurveFloor& fl : kFloors)
    {
        SCOPED_TRACE("floor " + std::to_string(fl.floor_number) +
                     " at crew level " + std::to_string(fl.crew_level));
        if (measure)
        {
            std::printf("TOWER CALIB floor=%d cl=%d survivors@600:",
                        fl.floor_number, fl.crew_level);
            for (std::uint32_t seed : kRunSeeds)
                std::printf(" %d", survivors_at_600(seed, fl.floor_number,
                                                    fl.crew_level));
            std::printf("\n");
            std::fflush(stdout);
            continue;
        }
        for (std::uint32_t seed : kRunSeeds)
        {
            const FloorBattle r =
                battle_at_600(seed, fl.floor_number, fl.crew_level);
            ASSERT_GE(r.alive, 0) << "floor failed to generate/load/deploy "
                                     "(run seed " << seed << ")";
            // §5.6: N(f) = min(7 + f, 30) foes, boss floors keeping 70 % and
            // up to a quarter carved off as team-3 raiders — a third of N is
            // slack under every one of those splits, and zero is not.
            EXPECT_GE(r.foes,
                      og::tower::foe_count_for_floor(fl.floor_number) / 3)
                << "run seed " << seed
                << ": the generated floor fielded almost nobody — the ramp "
                   "contract is that every floor is populated from N(f)";
            EXPECT_GT(r.crew_taken, 0.0f)
                << "run seed " << seed
                << ": the crew came home untouched: the floor's posts never "
                   "bit";
            EXPECT_GT(r.crew_dealt, 0.0f)
                << "run seed " << seed
                << ": the crew never landed a hit in 600 ticks: it is not "
                   "fighting the floor at all";
        }
    }
    prune_all_floors();
}
