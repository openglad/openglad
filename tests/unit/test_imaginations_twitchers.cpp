/* Stuck-twitcher regression net for the Imaginations campaign: the hunt
 * AI (ACT_RANDOM) beelines with no pathfinding, so any roamer whose
 * straight line to its foe crosses the isle's moat jams at the water's
 * edge, turning in place forever ("stuck and twitching" in playtests —
 * twice: first the placed roamers, then the college generators' spawns).
 * This test runs the level unattended and fails if ANY mobile awake
 * living spends a long window essentially stationary while its nearest
 * enemy is far away — fighting units (foe close) and posted guards
 * (ACT_GUARD) and immobile turrets are not twitchers.
 *
 * Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include "westlands_sim_fixture.h"

#include <cmath>
#include <map>

namespace {

using westlands_fixture::LoadedWestlandsLevel;
using westlands_fixture::MountedCampaignTest;
using westlands_fixture::deploy_crew;

class ImaginationsTwitchers : public MountedCampaignTest
{
protected:
    ImaginationsTwitchers()
        : MountedCampaignTest("imaginations")
    {
    }
};

// A living is a TWITCHER at a sample point when it is alive, awake,
// mobile (stepsize > 0), NOT holding a guard post, its nearest living
// enemy is beyond engagement range, and it has moved less than half a
// tile since the previous sample 300 ticks ago.
constexpr int kSampleTicks = 300;
constexpr int kSamples = 6; // 1800 ticks total
constexpr float kMoveEpsilon = 8.0f;        // < half a tile per window
constexpr float kEngageRange = 4.0f * 16.0f; // fighting, not stuck

} // namespace

TEST_F(ImaginationsTwitchers, no_living_jams_against_the_moat)
{
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "sim-shape pin enforced on the ci-test/coverage lanes";
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    GTEST_SKIP() << "sim-shape pin enforced on the ci-test/coverage lanes";
#endif
#endif
    LoadedWestlandsLevel fx(1, 42u);
    ASSERT_TRUE(fx.loaded);
    GameWorld& world = fx.world();
    std::vector<walker*> crew = deploy_crew(
        fx.level, world, {FAMILY_SOLDIER, FAMILY_SOLDIER, FAMILY_SOLDIER,
                          FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER,
                          FAMILY_CLERIC, FAMILY_BARBARIAN}, 1);
    ASSERT_EQ(8u, crew.size());
    // Park the crew where it landed (an idle human player, the situation
    // the twitchers were reported in): posted hold guards never move, so
    // every foe with a will to reach them must actually path there.
    for (walker* member : crew)
    {
        member->set_act_type(ACT_GUARD);
        member->set_guard_hold_post(true);
    }

    std::map<walker*, std::pair<float, float>> last_pos;
    for (int sample = 0; sample < kSamples; ++sample)
    {
        // Halfway through, march the parked crew to the north shore:
        // everything it woke in the south (once-woken guards hunt
        // forever) must either re-engage or settle back onto a post —
        // NOT jam twitching mid-field where its quarry used to be.
        if (sample == kSamples / 2)
        {
            for (std::size_t m = 0; m < crew.size(); ++m)
            {
                walker* member = crew[m];
                if (member == nullptr || member->dead())
                    continue;
                member->setxy(
                    static_cast<short>((26 + 2 * static_cast<int>(m)) * 16),
                    static_cast<short>(7 * 16));
            }
            last_pos.clear();
        }
        for (int t = 0; t < kSampleTicks; ++t)
            world.tick();
        for (const auto& uptr : world.oblist)
        {
            walker* ob = uptr.get();
            if (ob == nullptr || ob->query_order() != Order::Living ||
                ob->dead() || ob->spawn_delay() > 0)
                continue;
            if (ob->act_type() == ACT_GUARD || ob->stepsize() < 1.0f)
                continue;
            const float x = static_cast<float>(ob->xpos());
            const float y = static_cast<float>(ob->ypos());
            float nearest = 1e9f;
            for (const auto& fptr : world.oblist)
            {
                walker* foe = fptr.get();
                if (foe == nullptr || foe->query_order() != Order::Living ||
                    foe->dead() || foe->team_num() == ob->team_num())
                    continue;
                const float dx = static_cast<float>(foe->xpos()) - x;
                const float dy = static_cast<float>(foe->ypos()) - y;
                const float d = std::sqrt(dx * dx + dy * dy);
                if (d < nearest)
                    nearest = d;
            }
            const auto it = last_pos.find(ob);
            if (it != last_pos.end() && nearest > kEngageRange)
            {
                const float mdx = x - it->second.first;
                const float mdy = y - it->second.second;
                EXPECT_GE(std::sqrt(mdx * mdx + mdy * mdy), kMoveEpsilon)
                    << "twitcher: order Living family "
                    << static_cast<int>(ob->family()) << " team "
                    << static_cast<int>(ob->team_num()) << " at tile ("
                    << ob->xpos() / 16 << ", " << ob->ypos() / 16
                    << ") tick " << (sample + 1) * kSampleTicks
                    << " — stationary with no enemy near (moat jam)";
            }
            last_pos[ob] = {x, y};
        }
    }
}
