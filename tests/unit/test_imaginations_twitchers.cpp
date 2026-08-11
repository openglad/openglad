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
#include <cstdlib>
#include <map>
#include <set>

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
constexpr float kEngageRange = 1.5f * 16.0f; // in CONTACT; a foe merely
// nearby across the moat is exactly the twitch case, not an exemption

// --- Wake guarantee -----------------------------------------------------
// The mirror image of the twitcher net: a post must WAKE. Every hostile
// class the isle fields — placed sentry, college spawn, script-re-posted
// hunter — must ATTACK an enemy standing two tiles away on open ground
// within kWakeWindowTicks: either the victim loses hitpoints or the
// subject leaves ACT_GUARD and closes on it. The shipped level failed
// this for every placed foe: the shared mapgen builders stamp hold-post
// on team<=1 guards (the allied-escort rule), so walker::act_guard never
// converted the garrison to the hunt AI and its undirected COMMAND_FIRE
// died on the facing gate — sit-and-twitch, forever, with prey adjacent.
constexpr int kWakeWindowTicks = 300;

walker* find_placed_living(GameWorld& world, int family, int tx, int ty)
{
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            !ob->dead() && ob->family() == family &&
            ob->xpos() == tx * GRID_SIZE && ob->ypos() == ty * GRID_SIZE)
            return ob;
    }
    return nullptr;
}

// A tough, passive, non-regenerating team-0 victim: "an enemy standing
// two tiles away on open ground". Never NORTH of the subject — the
// undirected COMMAND_FIRE a held post queues aims FACE_UP, so an east
// or west victim can never be hit by the pre-fix statue by accident;
// landed damage always means a genuine engagement.
walker* spawn_victim(GameWorld& world, int tx, int ty)
{
    walker* v = world.add_ob(Order::Living, FAMILY_SOLDIER);
    if (v == nullptr)
        return nullptr;
    v->set_team_num(0);
    v->set_real_team_num(0);
    v->set_act_type(ACT_SIT);
    v->set_floor(0);
    v->setxy(static_cast<short>(tx * GRID_SIZE),
             static_cast<short>(ty * GRID_SIZE));
    v->stats()->set_max_hitpoints(30000.0f);
    v->stats()->set_hitpoints(30000.0f);
    v->stats()->set_heal_per_round(0.0f);
    return v;
}

int manhattan_px(const walker* a, const walker* b)
{
    return std::abs(static_cast<int>(a->xpos()) -
                    static_cast<int>(b->xpos())) +
           std::abs(static_cast<int>(a->ypos()) -
                    static_cast<int>(b->ypos()));
}

struct WakeResult
{
    bool woke = false;        // subject reached ACT_RANDOM at some tick
    bool closed = false;      // subject ended >= half a tile nearer
    bool victim_hurt = false; // the victim lost hitpoints
    bool attacked() const { return victim_hurt || (woke && closed); }
};

WakeResult observe_attack(GameWorld& world, walker* subject, walker* victim)
{
    const float hp0 = victim->stats()->hitpoints();
    const int dist0 = manhattan_px(subject, victim);
    WakeResult r;
    for (int t = 0; t < kWakeWindowTicks; ++t)
    {
        world.tick();
        if (subject->act_type() == ACT_RANDOM)
            r.woke = true;
    }
    r.closed = manhattan_px(subject, victim) <= dist0 - 8;
    r.victim_hurt = victim->stats()->hitpoints() < hp0 - 0.25f;
    return r;
}

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
    // Park the crew ON the south assault line, two tiles off the moat —
    // where a real player stands while fighting at the causeway. Posted
    // hold guards never move, so every woken foe across the water has
    // live prey in close sight the whole run.
    {
        static constexpr int kLineX[8] = {24, 26, 28, 34, 36, 38, 40, 22};
        for (std::size_t m = 0; m < crew.size(); ++m)
        {
            walker* member = crew[m];
            member->setxy(static_cast<short>(kLineX[m] * 16),
                          static_cast<short>(48 * 16));
            member->set_act_type(ACT_GUARD);
            member->set_guard_hold_post(true);
        }
    }

    // Aggro the whole island the way a real player does: every placed
    // hostile wakes into the hunt AI. The dream script must stand the
    // unreachable ones back down; anything left beelining at prey it
    // cannot reach shows up as a stationary far-from-foe twitcher.
    for (const auto& uptr : world.oblist)
    {
        walker* ob = uptr.get();
        if (ob != nullptr && ob->query_order() == Order::Living &&
            !ob->dead() && ob->team_num() == 1 && ob->spawn_delay() == 0 &&
            ob->stepsize() >= 1.0f)
            ob->set_act_type(ACT_RANDOM);
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

// The wake guarantee, per hostile class. Each scenario loads a fresh
// level (seed 42) so subjects cannot contaminate each other, parks a
// victim two tiles EAST, and requires an attack within the window.
// Placed sentries run in situ — this is literally the shipped bug's
// screenshot: the player's unit beside a posted guard in open field.
TEST_F(ImaginationsTwitchers, posted_hostiles_wake_and_attack_adjacent_enemy)
{
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
    GTEST_SKIP() << "sim-shape pin enforced on the ci-test/coverage lanes";
#elif defined(__has_feature)
#if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    GTEST_SKIP() << "sim-shape pin enforced on the ci-test/coverage lanes";
#endif
#endif
    // (1) PLACED SENTRIES, one per garrison art: a causeway head, the
    // bailey orc and archer, and an immobile watchtower (which cannot
    // close and must therefore land arrows — its walkstep is a turn).
    // The orc's victim stands WEST (two tiles east of its ring corner
    // is the bailey wall).
    struct PlacedSentry
    {
        const char* label;
        int family;
        int tx;
        int ty;
        int victim_dx;
    };
    static constexpr PlacedSentry kSentries[] = {
        {"north causeway skeleton", FAMILY_SKELETON, 31, 13, 2},
        {"bailey orc", FAMILY_ORC, 39, 39, -2},
        {"bailey elf", FAMILY_ELF, 31, 24, 2},
        {"bailey watchtower", FAMILY_TOWER1, 38, 32, 2},
    };
    for (const PlacedSentry& s : kSentries)
    {
        LoadedWestlandsLevel fx(1, 42u);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        world.enemy_freeze = 0;
        world.end = 0;
        walker* subject = find_placed_living(world, s.family, s.tx, s.ty);
        ASSERT_NE(nullptr, subject) << s.label;
        ASSERT_EQ(ACT_GUARD, subject->act_type()) << s.label;
        walker* victim = spawn_victim(world, s.tx + s.victim_dx, s.ty);
        ASSERT_NE(nullptr, victim) << s.label;
        const WakeResult r = observe_attack(world, subject, victim);
        EXPECT_TRUE(r.attacked())
            << s.label << " at (" << s.tx << ", " << s.ty
            << ") ignored an enemy two tiles "
            << (s.victim_dx > 0 ? "east" : "west") << " for "
            << kWakeWindowTicks << " ticks (woke=" << r.woke
            << " closed=" << r.closed << " victim_hurt=" << r.victim_hurt
            << ") — the sit-and-twitch statue bug";
    }

    // (2) COLLEGE SPAWN: generator spawns are born posted
    // (customize_spawn). Take the first hostile spawn a college
    // produces, re-post it on a clean meadow pad (its birth cell is a
    // brawl of college mates and Dreamkin), and require the same wake.
    {
        LoadedWestlandsLevel fx(1, 42u);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        world.enemy_freeze = 0;
        world.end = 0;
        std::set<const walker*> placed;
        for (const auto& uptr : world.oblist)
            placed.insert(uptr.get());
        walker* spawn = nullptr;
        for (int t = 0; t < 900 && spawn == nullptr; ++t)
        {
            world.tick();
            for (const auto& uptr : world.oblist)
            {
                walker* ob = uptr.get();
                if (ob != nullptr && ob->query_order() == Order::Living &&
                    !ob->dead() && ob->team_num() == 1 &&
                    placed.find(ob) == placed.end())
                {
                    spawn = ob;
                    break;
                }
            }
        }
        ASSERT_NE(nullptr, spawn) << "no college produced a hostile spawn";
        spawn->stats()->clear_command();
        spawn->set_floor(0);
        spawn->setxy(static_cast<short>(14 * GRID_SIZE),
                     static_cast<short>(14 * GRID_SIZE));
        spawn->set_act_type(ACT_GUARD);
        walker* victim = spawn_victim(world, 16, 14);
        ASSERT_NE(nullptr, victim);
        const WakeResult r = observe_attack(world, spawn, victim);
        EXPECT_TRUE(r.attacked())
            << "college spawn (family " << static_cast<int>(spawn->family())
            << ") posted on the northwest meadow ignored an enemy two tiles"
            << " east (woke=" << r.woke << " closed=" << r.closed
            << " victim_hurt=" << r.victim_hurt << ")";
    }

    // (3) RE-POSTED HUNTER: a woken sentry the dream script stands back
    // down (the mod-50 reposter) must be a real post afterwards, not a
    // statue. Pin the hunter to its post while waiting for the script
    // (a free hunter drifts toward the colleges), then drop the stale
    // hunt commands — do_command outranks the act switch, and what is
    // under test is the re-wake, not the leftover chase — and require
    // the standard wake against a fresh adjacent enemy.
    {
        LoadedWestlandsLevel fx(1, 42u);
        ASSERT_TRUE(fx.loaded);
        GameWorld& world = fx.world();
        world.enemy_freeze = 0;
        world.end = 0;
        walker* subject = find_placed_living(world, FAMILY_ORC, 39, 39);
        ASSERT_NE(nullptr, subject);
        subject->set_act_type(ACT_RANDOM); // a woken hunter
        bool demoted = false;
        for (int t = 0; t < 121 && !demoted; ++t)
        {
            world.tick();
            subject->setxy(static_cast<short>(39 * GRID_SIZE),
                           static_cast<short>(39 * GRID_SIZE));
            if (subject->act_type() == ACT_GUARD)
                demoted = true;
        }
        ASSERT_TRUE(demoted)
            << "the dream script must stand a disengaged hunter down";
        subject->stats()->clear_command();
        walker* victim = spawn_victim(world, 37, 39);
        ASSERT_NE(nullptr, victim);
        const WakeResult r = observe_attack(world, subject, victim);
        EXPECT_TRUE(r.attacked())
            << "re-posted bailey orc ignored an enemy two tiles west"
            << " (woke=" << r.woke << " closed=" << r.closed
            << " victim_hurt=" << r.victim_hurt
            << ") — a script stand-down must stay wakeable";
    }
}

// A saved company member on a foreign roster color (yellow, team 3) is
// a player's hero without a local seat, not garrison: the dream
// script's reposter must never post it. Pre-fix it matched the
// "hostile hunter" filter (team ~= 0) and was parked at the first
// mod-50 tick, freezing the player's own character on the beach.
TEST_F(ImaginationsTwitchers, company_members_are_never_reposted)
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
    std::vector<walker*> crew =
        deploy_crew(fx.level, world, {FAMILY_SOLDIER}, 1);
    ASSERT_EQ(1u, crew.size());
    walker* member = crew[0];
    ASSERT_NE(nullptr, member->myguy);
    member->set_team_num(3);
    member->set_real_team_num(3);
    member->myguy->teamnum = 3;
    member->set_act_type(ACT_RANDOM);
    // Pin the member on the west sand ring, far outside every wake
    // diamond, across >= 2 reposter periods: the engine gate can then
    // never re-wake a parked GUARD, so any flip off ACT_RANDOM here is
    // the reposter parking a player's hero.
    bool reposted = false;
    for (int t = 0; t < 130; ++t)
    {
        member->set_floor(0);
        member->setxy(static_cast<short>(7 * GRID_SIZE),
                      static_cast<short>(20 * GRID_SIZE));
        world.tick();
        if (member->act_type() != ACT_RANDOM)
            reposted = true;
    }
    EXPECT_FALSE(reposted)
        << "the isle reposter parked a saved company member (team 3, "
           "has_guy) — foreign-color heroes must keep hunting";
}
