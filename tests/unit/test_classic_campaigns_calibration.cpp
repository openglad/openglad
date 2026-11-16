/* The classic campaigns (Gladiator, The Tryxian Chronicles) under the
 * stand-in crew of the campaign calibration suites.
 *
 * The generated campaigns each have a calibration suite
 * (test_westlands_calibration.cpp and its siblings); the two classic
 * campaigns had none, so nothing measured what a change of the bots does
 * to them. This suite plays them through the same fixture: the 8-unit
 * mixed fresh team (4 soldiers, elf, archer, cleric, barbarian) deployed
 * by westlands_fixture::mixed_crew_battle onto the level's start markers.
 *
 * Default mode (what CI runs): for every level the mounted builtin
 * campaign lists (list_levels_v(), read from the campaign, never typed),
 * the level loads and the stand-in eight deploy (one tick of
 * mixed_crew_battle answers alive >= 0), and the number of levels that do
 * is the number the campaign lists, which is not zero.
 *
 * Measure mode: CLASSIC_CALIBRATION_MEASURE=1 with OG_CALIBRATION_SEEDS
 * (westlands_sim_fixture.h, calibration_seeds()) plays each level for 600
 * ticks at its crew level from kClassicCrewLevel on every listed seed and
 * prints two lines per level:
 *   CALIBN campaign=<id> level=<id> cl=<n> markers=<m> survivors@600: ...
 *   CALIBN-TAKEN campaign=<id> level=<id> cl=<n> markers=<m> taken@600: ...
 * markers is how many start markers the crew deploys onto (team 0's
 * first, then any team's; at most eight). A crew member left without one
 * is teleported by deploy_crew, which draws from the random stream, so a
 * level with fewer than eight markers is played by a partly teleported
 * crew. scripts/calibration/seed_table.py runs this mode.
 *
 * Fit mode: CLASSIC_CALIBRATION_FIT=1 plays each level at crew levels 1
 * to 15 on seeds 42, 1337 and 2025 with the shipped default (Gameplay
 * Fixes on, New Specials on) and prints, per level, the three-seed average
 * at each crew level and the crew level whose average is nearest four
 * survivors (the lower one on a tie), as rows for kClassicCrewLevel.
 *
 * Neither the measure nor the fit mode asserts a survivor number: they
 * are measuring tools for balance work and run only when asked.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "westlands_sim_fixture.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <string_view>
#include <vector>

namespace {

using westlands_fixture::CurveBattle;

constexpr int kCalibrationTicks = 600;
// mixed_crew_battle's stand-in crew: four soldiers, an elf, an archer, a
// cleric and a barbarian.
constexpr int kCrewSize = 8;

struct ClassicCrewLevel
{
    const char* campaign;
    int level_id;
    int crew_level;
};

// A measuring device, fitted on 2026-11-16 on the game of d6d0741b7 with
// Gameplay Fixes on: the crew level at which the stand-in eight's
// three-seed average is nearest four survivors. It claims nothing about
// what crew a player brings to these levels, and no test asserts a
// survivor number.
//
// The fit mode below wrote these rows (crew levels 1 to 15, seeds 42,
// 1337 and 2025, the lower level on a tie). Where no crew level comes
// near four, because the eight stand at every level or fall at every
// level, the row holds the nearest one there was, and the measure mode
// reads near 8 or near 0 on that level.
constexpr ClassicCrewLevel kClassicCrewLevel[] = {
    {"gladiator", 1, 1},   {"gladiator", 2, 1},   {"gladiator", 3, 1},
    {"gladiator", 4, 1},   {"gladiator", 5, 1},   {"gladiator", 6, 4},
    {"gladiator", 7, 2},   {"gladiator", 8, 7},   {"gladiator", 9, 3},
    {"gladiator", 10, 9},  {"gladiator", 11, 9},  {"gladiator", 12, 1},
    {"gladiator", 13, 12}, {"gladiator", 14, 3},  {"gladiator", 15, 2},
    {"gladiator", 16, 15}, {"gladiator", 17, 2},  {"gladiator", 18, 1},
    {"gladiator", 19, 7},  {"gladiator", 20, 10}, {"gladiator", 21, 12},
    {"gladiator", 22, 8},  {"gladiator", 23, 15}, {"gladiator", 24, 4},
    {"gladiator", 25, 4},  {"gladiator", 30, 5},  {"gladiator", 35, 10},
    {"gladiator", 36, 9},  {"gladiator", 37, 13}, {"gladiator", 38, 1},
    {"gladiator", 39, 10}, {"gladiator", 40, 15}, {"gladiator", 41, 3},
    {"gladiator", 42, 5},  {"gladiator", 43, 2},  {"gladiator", 44, 8},
    {"gladiator", 45, 2},
    {"tryxian", 103, 5},   {"tryxian", 104, 1},   {"tryxian", 105, 13},
    {"tryxian", 106, 3},   {"tryxian", 107, 5},   {"tryxian", 108, 14},
    {"tryxian", 109, 15},  {"tryxian", 110, 7},   {"tryxian", 111, 3},
    {"tryxian", 112, 6},   {"tryxian", 113, 1},   {"tryxian", 114, 13},
    {"tryxian", 115, 6},
};

// The fitted crew level of a level, or 0 when the table has no row for it.
int fitted_crew_level(std::string_view campaign, int level_id)
{
    for (const ClassicCrewLevel& row : kClassicCrewLevel)
    {
        if (campaign == row.campaign && level_id == row.level_id)
            return row.crew_level;
    }
    return 0;
}

// How many start markers deploy_crew puts the crew on for this level: every
// live FAMILY_RESERVED_TEAM marker counts (team 0's are taken first, then
// any team's), up to the crew's size. -1 when the level does not load.
int crew_markers(int level_id)
{
    westlands_fixture::LoadedWestlandsLevel fx(level_id, 42u);
    if (!fx.loaded)
        return -1;
    int markers = 0;
    for (const auto& entity : fx.world().oblist)
    {
        if (westlands_fixture::is_crew_start_marker(entity.get()))
            ++markers;
    }
    return std::min(markers, kCrewSize);
}

class ClassicCampaignsCalibration : public ::testing::Test
{
protected:
    // Mounts builtin/<campaign_id>.glad for the rest of the test and puts
    // the previously mounted campaign back afterwards (the
    // westlands_fixture::MountedCampaignTest shape, with the campaign
    // chosen by the test).
    void mount(const char* campaign_id)
    {
        restore_default_campaigns();
        previous_ = get_mounted_campaign();
        campaign_id_ = campaign_id;
        ASSERT_EQ(CampaignPackageIoError::None,
                  mount_campaign_package_with_error(campaign_id_))
            << "builtin/" << campaign_id_ << ".glad should restore and mount";
    }

    void TearDown() override
    {
        if (campaign_id_.empty())
            return;
        (void)unmount_campaign_package_with_error(campaign_id_);
        if (!previous_.empty())
            (void)mount_campaign_package_with_error(previous_);
    }

    // Every level of the mounted campaign loads and the stand-in eight
    // deploy on it; or, when asked, the measure or the fit mode.
    void check_campaign(const char* campaign_id)
    {
        ASSERT_NO_FATAL_FAILURE(mount(campaign_id));
        const std::vector<int> levels = list_levels_v();
        ASSERT_FALSE(levels.empty())
            << campaign_id << " lists no level: nothing would be checked";

        const bool measure =
            std::getenv("CLASSIC_CALIBRATION_MEASURE") != nullptr;
        const bool fit = std::getenv("CLASSIC_CALIBRATION_FIT") != nullptr;
        ASSERT_FALSE(measure && fit)
            << "CLASSIC_CALIBRATION_MEASURE and CLASSIC_CALIBRATION_FIT "
               "are two separate runs";
        if (measure)
        {
            measure_campaign(campaign_id, levels);
            return;
        }
        if (fit)
        {
            fit_campaign(campaign_id, levels);
            return;
        }

        std::size_t deployed = 0;
        for (const int level_id : levels)
        {
            const CurveBattle r =
                westlands_fixture::mixed_crew_battle(level_id, 1, 42u, 1);
            EXPECT_GE(r.alive, 0)
                << campaign_id << " scen" << level_id
                << " failed to load or to deploy the stand-in eight";
            if (r.alive >= 0)
                ++deployed;
        }
        EXPECT_EQ(levels.size(), deployed)
            << "every level " << campaign_id
            << " lists must load and deploy the crew";
    }

private:
    static void measure_campaign(const char* campaign_id,
                                 const std::vector<int>& levels)
    {
        const std::vector<std::uint32_t> seeds =
            westlands_fixture::calibration_seeds();
        if (::testing::Test::HasFailure())
            return; // calibration_seeds() named what is wrong with the list
        if (seeds.empty())
        {
            ADD_FAILURE() << "CLASSIC_CALIBRATION_MEASURE needs the seeds in "
                             "OG_CALIBRATION_SEEDS";
            return;
        }
        for (const int level_id : levels)
        {
            const int crew_level = fitted_crew_level(campaign_id, level_id);
            if (crew_level == 0)
            {
                ADD_FAILURE() << campaign_id << " scen" << level_id
                              << " has no row in kClassicCrewLevel: run the "
                                 "fit mode and add it";
                continue;
            }
            const int markers = crew_markers(level_id);
            std::vector<int> alive;
            std::vector<float> taken;
            for (const std::uint32_t seed : seeds)
            {
                const CurveBattle b = westlands_fixture::mixed_crew_battle(
                    level_id, crew_level, seed, kCalibrationTicks);
                alive.push_back(b.alive);
                taken.push_back(b.crew_taken);
            }
            westlands_fixture::print_calibn_lines(
                "",
                std::format("campaign={} level={} cl={} markers={}",
                            campaign_id, level_id, crew_level, markers),
                alive, taken);
        }
    }

    static void fit_campaign(const char* campaign_id,
                             const std::vector<int>& levels)
    {
        if (westlands_fixture::calibration_setting(
                "OG_CALIBRATION_GAMEPLAY_FIXES") != 1 ||
            westlands_fixture::calibration_setting(
                "OG_CALIBRATION_NEW_SPECIALS") != 1)
        {
            ADD_FAILURE() << "the fit is made on the shipped default: unset "
                             "OG_CALIBRATION_GAMEPLAY_FIXES and "
                             "OG_CALIBRATION_NEW_SPECIALS";
            return;
        }
        constexpr std::uint32_t kFitSeeds[] = {42u, 1337u, 2025u};
        constexpr int kFitCrewLevels = 15;
        // Four survivors on average over the three seeds: a sum of twelve.
        constexpr int kTargetSum = 4 * 3;
        for (const int level_id : levels)
        {
            std::string averages;
            int best_level = 0;
            int best_distance = 0;
            for (int crew_level = 1; crew_level <= kFitCrewLevels; ++crew_level)
            {
                int sum = 0;
                for (const std::uint32_t seed : kFitSeeds)
                {
                    const CurveBattle b = westlands_fixture::mixed_crew_battle(
                        level_id, crew_level, seed, kCalibrationTicks);
                    if (b.alive < 0)
                        ADD_FAILURE() << campaign_id << " scen" << level_id
                                      << " failed to load or deploy";
                    sum += b.alive;
                }
                const int distance = std::abs(sum - kTargetSum);
                // Strictly nearer only: on a tie the lower level stays.
                if (best_level == 0 || distance < best_distance)
                {
                    best_level = crew_level;
                    best_distance = distance;
                }
                averages += std::format(" {}:{:.2f}", crew_level, sum / 3.0);
            }
            std::printf("CLASSIC-FIT campaign=%s level=%d avg@cl:%s "
                        "nearest4=%d\n",
                        campaign_id, level_id, averages.c_str(), best_level);
            std::printf("CLASSIC-ROW     {\"%s\", %d, %d},\n", campaign_id,
                        level_id, best_level);
            std::fflush(stdout);
        }
    }

    std::string campaign_id_;
    std::string previous_;
};

} // namespace

TEST_F(ClassicCampaignsCalibration, gladiator_levels_load_and_deploy_the_crew)
{
    ASSERT_NO_FATAL_FAILURE(check_campaign("gladiator"));
}

TEST_F(ClassicCampaignsCalibration, tryxian_levels_load_and_deploy_the_crew)
{
    ASSERT_NO_FATAL_FAILURE(check_campaign("tryxian"));
}
