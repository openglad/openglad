/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Specials, ghost: WAIL (Shift + SCARE), SIPHON, POSSESS and PHASE
// (packs/core/lib/kit_ghost.lua, lib/effect_wail.lua). Every cast goes
// through the real walker::special(), so the price gate and the charge are
// part of what is pinned. Each special has an OFF twin: with the setting off
// a new slot is refused for mana before any Lua runs (the specials view
// prices it at 5000), and Shift + SCARE is the classic SCARE.
//
// Each special's test names the change to the staged pack copy
// (build/ci-test/packs/core/...) that was made once to watch it fail, with
// the failure it printed. Restaging (--target stage_runtime_assets) undoes it.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/family_presentation.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/pixie_data.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/og_file.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

using Failure = walker::SpecialFailure;

constexpr unsigned char kGhostTeam = 0;
constexpr unsigned char kFoeTeam = 1;

int wail_family()
{
    return og::families::resolve_family_string_id(Order::FX, "core:wail");
}

int marker_family()
{
    return og::families::resolve_family_string_id(Order::FX,
                                                  "core:kit_marker");
}

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y, short level, float hp)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(x, y);
    w->stats()->set_level(level);
    w->stats()->set_max_hitpoints(hp);
    w->stats()->set_hitpoints(hp);
    // A posted guard: it acts, but it is no seat, so it can be possessed.
    w->set_act_type(ACT_GUARD);
    return w;
}

walker* add_ghost(TestGameWorld& tw, short level, float mp, short x = 64,
                  short y = 64)
{
    walker* g = add_living(tw, FAMILY_GHOST, kGhostTeam, x, y, level, 100.0f);
    if (g == nullptr)
        return nullptr;
    g->stats()->set_max_magicpoints(mp);
    g->stats()->set_magicpoints(mp);
    return g;
}

struct CastResult {
    bool ok = false;
    Failure why = Failure::None;
    std::string reason;
};

CastResult cast(walker* w, int slot, bool shift)
{
    w->set_current_special(static_cast<char>(slot));
    w->set_shifter_down(shift ? 1 : 0);
    CastResult r;
    r.ok = w->special(&r.why, &r.reason);
    return r;
}

std::vector<walker*> live_of(TestGameWorld& tw, Order order, int family)
{
    std::vector<walker*> out;
    for (const auto& uptr : tw.world().oblist)
        if (uptr && !uptr->dead() && uptr->query_order() == order &&
            uptr->family() == family)
            out.push_back(uptr.get());
    return out;
}

int ever_made(TestGameWorld& tw, Order order, int family)
{
    int n = 0;
    for (const auto& uptr : tw.world().oblist)
        if (uptr && uptr->query_order() == order && uptr->family() == family)
            ++n;
    return n;
}

// Acts every live wail bolt, in list order, until none is left (bounded).
int run_wails(TestGameWorld& tw)
{
    int rounds = 0;
    for (; rounds < 400; ++rounds) {
        const std::vector<walker*> bolts = live_of(tw, Order::FX, wail_family());
        if (bolts.empty())
            break;
        for (walker* bolt : bolts)
            if (!bolt->dead())
                bolt->act();
    }
    return rounds;
}

bool notified(const TestGameWorld& tw, const std::string& needle)
{
    const auto& events = tw.events.events();
    return std::any_of(events.begin(), events.end(), [&](const auto& e) {
        return e.kind == og::sim::EventKind::Notification &&
               e.text.find(needle) != std::string::npos;
    });
}

bool in_obmap(TestGameWorld& tw, walker* w)
{
    return tw.world().myobmap->walker_to_pos.count(w) != 0;
}

// The first world-RNG state from which the draws satisfy `want` (the sim
// calls the LCG inline, so tests pin the state, not a spy).
std::uint32_t state_where(const std::function<bool(og::sim::SimRandom&)>& want)
{
    for (std::uint32_t seed = 1; seed < 2000000u; ++seed) {
        og::sim::SimRandom probe(seed);
        if (want(probe))
            return seed;
    }
    return 0;
}

}  // namespace

// ---------------------------------------------------------------------------
// WAIL
// ---------------------------------------------------------------------------

// Three orcs in a row, 90 px apart. The wail flies to the nearest, frightens
// it, and forks on to the next one along (the orc behind it is out of the
// 120 px fork reach), which forks on to the last; the last has no foe left
// that was not wailed at in the last three rounds, so the chain ends. Each
// strike adds 3 to the struck orc's skip_exit, so 3 on every orc means each
// was frightened exactly once.
//
// Perturbation: wail_fork_px = 0 in the staged effect-14-wail.lua tuning.
// RED: "the second orc was frightened" and "the third orc was frightened"
// (has_commands false), skip_exit 0 on both, and 1 bolt made, not 3.
// Perturbation: wail_bolt_step = 60. RED: "at wail_bolt_step px a tick"
// (stepsize 60, not 6) and the hops took 8 rounds, not 38.
// Perturbation: the fork reach counted from the puff's corner again
// (`+ self:distance_to_ob(leader)` dropped in the staged effect_wail.lua).
// RED: "the second orc was frightened" false, 1 bolt made, not 3.
TEST(KitGhost, wail_hops_foe_to_foe_and_frights_each_once)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 10, 100.0f, 100, 100);
    walker* a = add_living(tw, FAMILY_ORC, kFoeTeam, 140, 100, 1, 140.0f);
    walker* b = add_living(tw, FAMILY_ORC, kFoeTeam, 230, 100, 1, 140.0f);
    walker* c = add_living(tw, FAMILY_ORC, kFoeTeam, 320, 100, 1, 140.0f);
    ASSERT_NE(nullptr, ghost);
    ASSERT_NE(nullptr, c);

    const CastResult r = cast(ghost, 1, true);
    ASSERT_TRUE(r.ok) << r.reason;
    EXPECT_FLOAT_EQ(50.0f, ghost->stats()->magicpoints()) << "WAIL costs 50";
    const std::vector<walker*> first = live_of(tw, Order::FX, wail_family());
    ASSERT_EQ(1u, first.size());
    EXPECT_EQ(a, first[0]->leader()) << "the first bolt flies at the nearest";
    EXPECT_EQ(ghost, first[0]->owner());
    EXPECT_EQ(3, first[0]->lifetime()) << "it carries wail_hops hops";
    EXPECT_EQ(60, first[0]->lineofsight()) << "it drifts wail_bolt_life ticks";
    EXPECT_FLOAT_EQ(6.0f, first[0]->stepsize()) << "at wail_bolt_step px a tick";
    EXPECT_EQ(0, ever_made(tw, Order::FX, FAMILY_GHOST_SCARE))
        << "a wail is not a scare";

    const int rounds = run_wails(tw);
    EXPECT_LT(rounds, 400) << "the chain ends on its own";
    EXPECT_EQ(38, rounds)
        << "the three hops take this many rounds at 6 px a tick";
    EXPECT_TRUE(a->stats()->has_commands()) << "the first orc was frightened";
    EXPECT_TRUE(b->stats()->has_commands()) << "the second orc was frightened";
    EXPECT_TRUE(c->stats()->has_commands()) << "the third orc was frightened";
    EXPECT_EQ(3, a->skip_exit()) << "struck once";
    EXPECT_EQ(3, b->skip_exit()) << "struck once";
    EXPECT_EQ(3, c->skip_exit()) << "struck once";
    EXPECT_EQ(3, ever_made(tw, Order::FX, wail_family()))
        << "one bolt per orc, no bolt back to an orc already wailed at";
    EXPECT_FLOAT_EQ(0.0f, a->stats()->hitpoints() - 140.0f)
        << "the wail frightens, it does not hurt";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A fearless foe (the warband's mark) is struck but not frightened; the
// chain still hops on past it to the next orc.
//
// The fearless skip itself is the engine's (the s_force_fright binding,
// pinned in test_kit_seams_small.cpp); this row
// pins that the wail goes through that binding and hops on past the brave.
TEST(KitGhost, wail_spares_a_fearless_foe)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 10, 100.0f, 100, 100);
    walker* brave = add_living(tw, FAMILY_ORC, kFoeTeam, 140, 100, 1, 140.0f);
    walker* other = add_living(tw, FAMILY_ORC, kFoeTeam, 230, 100, 1, 140.0f);
    ASSERT_NE(nullptr, other);
    brave->set_kit_state(KIT_FEARLESS);

    ASSERT_TRUE(cast(ghost, 1, true).ok);
    run_wails(tw);
    EXPECT_FALSE(brave->stats()->has_commands())
        << "the fearless orc holds its ground";
    EXPECT_EQ(3, brave->skip_exit()) << "it was struck all the same";
    EXPECT_TRUE(other->stats()->has_commands())
        << "the wail hopped on and frightened the next orc";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// WAIL is the priced alternate of SCARE: 50 shifted, SCARE's 30 unshifted.
// With the setting off Shift + SCARE is the classic SCARE at 30 and no bolt
// is made. With no foe in the scare radius the wail is refused for free.
//
// Perturbation: `new_kit = true` dropped from WAIL's alternate in the
// staged living-12-ghost.lua. RED, "flag 0 shift true": 50 MP left, not 70;
// 1 wail bolt, not 0; "off: Shift + SCARE is the classic scare" 0 scares.
TEST(KitGhost, wail_costs_50_and_is_scare_when_off)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short flag : {short{1}, short{0}}) {
        for (const bool shift : {true, false}) {
            TestGameWorld tw;
            tw.world().new_specials = flag;
            walker* ghost = add_ghost(tw, 10, 100.0f, 100, 100);
            add_living(tw, FAMILY_ORC, kFoeTeam, 140, 100, 1, 140.0f);
            const CastResult r = cast(ghost, 1, shift);
            ASSERT_TRUE(r.ok) << r.reason << " flag " << flag;
            const bool wail = flag != 0 && shift;
            EXPECT_FLOAT_EQ(wail ? 50.0f : 70.0f,
                            ghost->stats()->magicpoints())
                << "flag " << flag << " shift " << shift;
            EXPECT_EQ(wail ? 1 : 0, ever_made(tw, Order::FX, wail_family()))
                << "flag " << flag << " shift " << shift;
            EXPECT_EQ(wail ? 0 : 1, ever_made(tw, Order::FX, FAMILY_GHOST_SCARE))
                << (flag == 0 ? "off: Shift + SCARE is the classic scare"
                              : "on, unshifted: the classic scare");
        }
    }

    // No foe in reach: refused, nothing spent, nothing drawn.
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 10, 100.0f, 100, 100);
    add_living(tw, FAMILY_ORC, kFoeTeam, 600, 600, 1, 140.0f);
    const std::uint32_t rng_before = tw.world().rng_.state_;
    const CastResult r = cast(ghost, 1, true);
    EXPECT_FALSE(r.ok);
    EXPECT_EQ(Failure::ScriptDeclined, r.why);
    EXPECT_EQ("NO FOE TO WAIL AT", r.reason);
    EXPECT_FLOAT_EQ(100.0f, ghost->stats()->magicpoints());
    EXPECT_EQ(rng_before, tw.world().rng_.state_);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A bolt hand-made the way the cast makes one (lib/effect_wail.lua launch):
// owned by the ghost, harmless, homing on `foe` with `hops` hops left.
walker* hand_bolt(TestGameWorld& tw, walker* ghost, walker* foe, int hops)
{
    walker* bolt = tw.world().add_ob(Order::FX, wail_family());
    if (bolt == nullptr)
        return nullptr;
    bolt->set_owner(ghost);
    bolt->set_team_num(ghost->team_num());
    bolt->stats()->set_level(ghost->stats()->level());
    bolt->center_on(ghost);
    bolt->set_leader(foe);
    bolt->set_lineofsight(60);
    bolt->set_stepsize(6.0f);
    bolt->set_lifetime(static_cast<short>(hops));
    return bolt;
}

// The bolt's own exits and choices. It flies at a foe up and to the left
// (both homing arms), reads a hero's constitution for the resist, never
// forks with no hops left, never forks to a generator or across floors, and
// fades without a fright when its foe dies or hides on the way.
//
// Perturbation: is_fork_target's floor test deleted in the staged
// effect_wail.lua. RED: "never across floors" (the floor-1 orc frightened),
// 2 bolts made, not 1, then 3 not 2.
TEST(KitGhost, wail_bolt_homes_forks_only_where_it_may_and_fades)
{
    og::test::ScopedHookFailureGuard guard;
    {
        // Up-left foe carrying a hero record: the constitution draw is made.
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 10, 100.0f, 200, 200);
        walker* hero = add_living(tw, FAMILY_ORC, kFoeTeam, 120, 150, 1, 140.0f);
        hero->set_owned_myguy(std::make_unique<guy>(FAMILY_ORC));
        ASSERT_TRUE(hero->myguy != nullptr);
        const std::uint32_t before = tw.world().rng_.state_;
        ASSERT_TRUE(cast(ghost, 1, true).ok);
        run_wails(tw);
        EXPECT_TRUE(hero->stats()->has_commands()) << "the hero was frightened";
        EXPECT_NE(before, tw.world().rng_.state_) << "its constitution was rolled";
    }
    {
        // Floor, order and hop limits: the struck orc has a generator and an
        // orc on floor 1 beside it, and a second orc in reach; a bolt with
        // no hops left frightens and stops.
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 10, 100.0f, 100, 100);
        walker* a = add_living(tw, FAMILY_ORC, kFoeTeam, 140, 100, 1, 140.0f);
        walker* tent = tw.world().add_ob(Order::Generator, FAMILY_TENT);
        ASSERT_NE(nullptr, tent);
        tent->set_team_num(kFoeTeam);
        tent->setxy(170, 100);
        walker* upstairs = add_living(tw, FAMILY_ORC, kFoeTeam, 150, 120, 1, 140.0f);
        upstairs->set_floor(1);
        ASSERT_TRUE(cast(ghost, 1, true).ok);
        run_wails(tw);
        EXPECT_TRUE(a->stats()->has_commands());
        EXPECT_FALSE(upstairs->stats()->has_commands()) << "never across floors";
        EXPECT_EQ(1, ever_made(tw, Order::FX, wail_family()))
            << "never across floors, never to a generator";

        walker* b = add_living(tw, FAMILY_ORC, kFoeTeam, 200, 140, 1, 140.0f);
        walker* c = add_living(tw, FAMILY_ORC, kFoeTeam, 230, 140, 1, 140.0f);
        ASSERT_NE(nullptr, hand_bolt(tw, ghost, b, 0));
        run_wails(tw);
        EXPECT_TRUE(b->stats()->has_commands());
        EXPECT_FALSE(c->stats()->has_commands()) << "no hops left: no fork";
        EXPECT_EQ(2, ever_made(tw, Order::FX, wail_family()));
    }
    {
        // The foe dies, or hides, before the bolt arrives: no fright.
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 10, 100.0f, 100, 100);
        walker* doomed = add_living(tw, FAMILY_ORC, kFoeTeam, 220, 100, 1, 140.0f);
        walker* sinks = add_living(tw, FAMILY_ORC, kFoeTeam, 100, 220, 1, 140.0f);
        walker* to_doomed = hand_bolt(tw, ghost, doomed, 0);
        walker* to_sinks = hand_bolt(tw, ghost, sinks, 0);
        ASSERT_NE(nullptr, to_sinks);
        to_doomed->act();
        to_sinks->act();
        EXPECT_FALSE(to_doomed->dead());
        doomed->set_dead(1);
        sinks->set_hidden(true);
        to_doomed->act();
        to_sinks->act();
        EXPECT_TRUE(to_doomed->dead()) << "a bolt whose foe died fades";
        EXPECT_TRUE(to_sinks->dead()) << "a bolt whose foe hid fades";
        EXPECT_FALSE(sinks->stats()->has_commands());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The puff's 48x40 frame is mostly empty air: only an 8x8 core at its
// centre strikes. A bolt staged below and right of the orc, so its frame
// overlaps the orc's box while its core does not, is not struck on its
// first act; it steers its centre onto the orc's centre, 6 px an act up and
// left, and the strike comes on the act the run pinned below. Steering the
// puff's corner onto the orc's corner instead would park the core 4 px
// right of the orc for good.
//
// Perturbation: the frame-box test restored (hits(self:xpos(), self:ypos(),
// self:sizex(), self:sizey(), ...) in the staged effect_wail.lua). RED:
// struck on act 1, not 4.
// Perturbation: home_on steering the corner (tx = leader:xpos(), ty =
// leader:ypos()). RED: struck_on 0 (never), the orc not frightened,
// skip_exit 0.
TEST(KitGhost, wail_strikes_from_its_core_not_its_cloud)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 10, 100.0f, 100, 100);
    walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 200, 100, 1, 140.0f);
    walker* bolt = hand_bolt(tw, ghost, orc, 0);
    ASSERT_NE(nullptr, bolt);
    ASSERT_EQ(48, bolt->sizex());
    ASSERT_EQ(40, bolt->sizey());
    // Frame [210,258] x [110,150] overlaps the orc's [200,216] x [100,116];
    // the core [230,238] x [126,134] does not.
    bolt->setxy(210, 110);

    int struck_on = 0;
    for (int act = 1; act <= 60 && struck_on == 0; ++act) {
        bolt->act();
        if (bolt->dead())
            struck_on = act;
    }
    EXPECT_EQ(4, struck_on) << "the core reaches the orc on this act";
    EXPECT_TRUE(orc->stats()->has_commands()) << "the strike frightened the orc";
    EXPECT_EQ(3, orc->skip_exit()) << "struck once";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The fork reach (wail_fork_px, 120) is counted from the orc the puff
// struck, not from the puff's corner, which sits 16 px left of and 12 px
// above a centred orc's. An orc 130 px from the struck one, up and left
// (102 px from the puff's corner), is out of reach; one 115 px away down
// and right (143 px from the corner) is in reach.
//
// Perturbation: is_fork_target's distance test deleted in the staged
// effect_wail.lua. RED: "130 px from the struck orc" (frightened), its
// skip_exit 3.
// Perturbation: the reach counted from the puff (`+ self:distance_to_ob(
// leader)` dropped). RED: "115 px from the struck orc" (not frightened).
TEST(KitGhost, wail_forks_within_reach_of_the_struck_foe)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 10, 100.0f, 60, 60);
    walker* struck = add_living(tw, FAMILY_ORC, kFoeTeam, 300, 300, 1, 140.0f);
    // Listed before the near orc, so a fork that wrongly reached it would
    // take it first even with a single fork to spend.
    walker* far = add_living(tw, FAMILY_ORC, kFoeTeam, 200, 270, 1, 140.0f);
    walker* near = add_living(tw, FAMILY_ORC, kFoeTeam, 400, 315, 1, 140.0f);
    ASSERT_NE(nullptr, near);
    ASSERT_EQ(130, struck->distance_to_ob(far));
    ASSERT_EQ(115, struck->distance_to_ob(near));
    walker* bolt = hand_bolt(tw, ghost, struck, 1);
    ASSERT_NE(nullptr, bolt);
    bolt->setxy(284, 288);  // centred on the struck orc: strikes at once
    ASSERT_EQ(102, bolt->distance_to_ob(far));
    ASSERT_EQ(143, bolt->distance_to_ob(near));
    bolt->act();
    ASSERT_TRUE(bolt->dead()) << "struck on its first act";
    EXPECT_EQ(3, struck->skip_exit());
    run_wails(tw);
    EXPECT_TRUE(near->stats()->has_commands()) << "115 px from the struck orc";
    EXPECT_FALSE(far->stats()->has_commands()) << "130 px from the struck orc";
    EXPECT_EQ(0, far->skip_exit()) << "never struck";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The puff loops the scare's small sparkle frames, 1, 2, 3, 2, one frame
// per wail_frame_ticks (2) acts, off its own lineofsight count.
//
// The bolt's count starts at 60 and the frame is set after it drops, so the
// first act shows 59 / 2 = 29, 29 mod 4 = 1: SPARKLE_FRAMES[2] = 2. The
// sequence below is the one the run printed.
//
// Perturbation: wail_frame_ticks = 1 in the staged effect-14-wail.lua.
// RED: frames { 2, 3, 2, 1, 2, 3, 2, 1 }.
TEST(KitGhost, wail_cycles_the_small_sparkle_frames)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 10, 100.0f, 100, 100);
    walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 500, 100, 1, 140.0f);
    walker* bolt = hand_bolt(tw, ghost, orc, 0);
    ASSERT_NE(nullptr, bolt);
    std::vector<int> frames;
    for (int act = 0; act < 8; ++act) {
        bolt->act();
        ASSERT_FALSE(bolt->dead()) << "act " << act;
        frames.push_back(bolt->frame());
    }
    EXPECT_EQ((std::vector<int>{2, 2, 1, 1, 2, 2, 3, 3}), frames)
        << "the frame is set after the count drops: 59 / 2 = 29, 29 mod 4 = 1";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A full world: no bolt, no veil, no burst can be made. The casts that need
// one refuse for free; POSSESS still lands without its entrance burst.
TEST(KitGhost, a_full_world_refuses_the_casts_that_need_an_entity)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 10, 300.0f);
    walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
    const auto factory = tw.world().entity_factory;
    tw.world().entity_factory = [factory](Order order, std::int32_t family) {
        if (order == Order::FX)
            return std::unique_ptr<walker>();
        return factory(order, family);
    };
    CastResult r = cast(ghost, 1, true);
    EXPECT_EQ("COULD NOT CREATE WAIL", r.reason);
    r = cast(ghost, 1, false);
    EXPECT_EQ("COULD NOT CREATE SCARE", r.reason) << "the classic refusal";
    r = cast(ghost, 4, false);
    EXPECT_EQ("COULD NOT PHASE", r.reason);
    EXPECT_FALSE(ghost->stats()->query_bit_flags(BIT_PHANTOM));
    EXPECT_FLOAT_EQ(300.0f, ghost->stats()->magicpoints()) << "all refused free";

    tw.world().rng_.state_ = state_where([](og::sim::SimRandom& rng) {
        const std::uint32_t level_roll = rng.next(100);
        return rng.next(40) <= level_roll;
    });
    r = cast(ghost, 3, false);
    EXPECT_TRUE(r.ok) << r.reason;
    EXPECT_EQ(orc->entity_id(), ghost->possess_link()) << "lands all the same";
    tw.world().entity_factory = factory;
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// SIPHON
// ---------------------------------------------------------------------------

// A level-4 ghost's touch hits for 10 + 2 x 4 = 18 before armour; whatever
// the foe loses, the ghost gains half of (whole points), never past its
// maximum, and its own melee damage is back afterwards.
//
// Perturbation: og.fdiv(dealt, 2.0) -> og.fdiv(dealt, 1.0) in the staged
// kit_ghost.lua. RED: "half of what the orc lost": 36 hp, not 28 (the
// ghost drank all 16 it dealt).
TEST(KitGhost, siphon_heals_half_of_damage_dealt_never_above_max)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 4, 40.0f);
    walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
    ASSERT_NE(nullptr, orc);
    ghost->stats()->set_hitpoints(20.0f);
    const float own_damage = ghost->damage();

    const CastResult r = cast(ghost, 2, false);
    ASSERT_TRUE(r.ok) << r.reason;
    EXPECT_FLOAT_EQ(10.0f, ghost->stats()->magicpoints()) << "SIPHON costs 30";
    const float dealt = 140.0f - orc->stats()->hitpoints();
    ASSERT_GT(dealt, 1.0f) << "the touch hurt the orc";
    EXPECT_FLOAT_EQ(20.0f + static_cast<float>(static_cast<int>(dealt / 2.0f)),
                    ghost->stats()->hitpoints())
        << "half of what the orc lost";
    EXPECT_FLOAT_EQ(own_damage, ghost->damage())
        << "the touch's damage is swapped back out";

    // Nearly full: the heal stops at the maximum.
    ghost->stats()->set_hitpoints(99.0f);
    ghost->stats()->set_magicpoints(40.0f);
    ASSERT_TRUE(cast(ghost, 2, false).ok);
    EXPECT_FLOAT_EQ(100.0f, ghost->stats()->hitpoints())
        << "never above max hp";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A touch that finds nobody, or a foe that cannot be hurt, still costs the
// 30 mana: SIPHON is not a free probe. With the setting off the slot does
// not exist (refused for mana, nothing spent).
//
// Perturbation: the empty-touch arm returns `false, "NOTHING"` in the
// staged kit_ghost.lua. RED: "an empty touch still spends" (r.ok false)
// and 40 MP left, not 10.
TEST(KitGhost, empty_siphon_still_spends)
{
    og::test::ScopedHookFailureGuard guard;
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 4, 40.0f);
        ghost->stats()->set_hitpoints(50.0f);
        const CastResult r = cast(ghost, 2, false);
        EXPECT_TRUE(r.ok) << "an empty touch still spends";
        EXPECT_FLOAT_EQ(10.0f, ghost->stats()->magicpoints());
        EXPECT_FLOAT_EQ(50.0f, ghost->stats()->hitpoints());
        EXPECT_TRUE(notified(tw, "touch finds nothing"));
    }
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 4, 40.0f);
        ghost->stats()->set_hitpoints(50.0f);
        walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
        orc->set_invulnerable_left(20);
        const CastResult r = cast(ghost, 2, false);
        EXPECT_TRUE(r.ok) << "a resisted touch still spends";
        EXPECT_FLOAT_EQ(10.0f, ghost->stats()->magicpoints());
        EXPECT_FLOAT_EQ(140.0f, orc->stats()->hitpoints());
        EXPECT_FLOAT_EQ(50.0f, ghost->stats()->hitpoints()) << "nothing to drink";
    }
    {
        TestGameWorld tw;  // OFF twin
        walker* ghost = add_ghost(tw, 4, 40.0f);
        ghost->stats()->set_hitpoints(50.0f);
        walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
        const CastResult r = cast(ghost, 2, false);
        EXPECT_FALSE(r.ok);
        EXPECT_EQ(Failure::NoMP, r.why) << "off: SIPHON is not in play";
        EXPECT_FLOAT_EQ(40.0f, ghost->stats()->magicpoints());
        EXPECT_FLOAT_EQ(140.0f, orc->stats()->hitpoints());
        EXPECT_FLOAT_EQ(50.0f, ghost->stats()->hitpoints());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// POSSESS
// ---------------------------------------------------------------------------

// The resist roll is the orc howl's: the level roll is drawn FIRST, then the
// constitution roll, and the host resists when its roll is higher. The
// state below is chosen so that order resists and the other order would
// not, so a swapped draw order turns this red. A resisted touch spends the
// mana and the host strikes back; a landed one rides the host for
// 120 + 40 x (7 - 1) = 360 ticks.
//
// Perturbation: the two roll lines swapped in the staged kit_ghost.lua
// (constitution drawn first). RED: "the host resisted" (possess_link 2, not
// 0), the ghost hidden, the orc on the ghost's team, "the host turns on the
// ghost" and "resisted!" both missing.
// A second RED, before the cast held the ghost's mana aside for the host's
// blow: "a resisted touch spends the 50" read 0 (the struck bot ghost cast
// POSSESS again from inside the first cast, and both were charged).
TEST(KitGhost, possess_resist_roll_uses_howl_order)
{
    og::test::ScopedHookFailureGuard guard;
    // Orc: 140 hp, no guy -> con = trunc(140 / 30) = 4 -> roll over 40.
    // Ghost level 7 -> roll over 70.
    // A state where the level roll then the constitution roll resists, and
    // the same state drawn the other way round would not.
    std::uint32_t order_proof = 0;
    for (std::uint32_t seed = 1; seed < 2000000u && order_proof == 0; ++seed) {
        og::sim::SimRandom left(seed);
        const std::uint32_t level_roll = left.next(70);
        const std::uint32_t con_roll = left.next(40);
        og::sim::SimRandom right(seed);
        const std::uint32_t con_first = right.next(40);
        const std::uint32_t level_second = right.next(70);
        if (con_roll > level_roll && !(con_first > level_second))
            order_proof = seed;
    }
    ASSERT_NE(0u, order_proof);
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        // A bot ghost: struck by the host's blow, it answers with a special
        // of its own (stats.cpp's hit response) unless the cast holds its
        // mana aside for the blow; 50 left proves no second cast ran.
        walker* ghost = add_ghost(tw, 7, 100.0f);
        walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
        tw.world().rng_.state_ = order_proof;
        const CastResult r = cast(ghost, 3, false);
        ASSERT_TRUE(r.ok) << r.reason;
        EXPECT_FLOAT_EQ(50.0f, ghost->stats()->magicpoints())
            << "a resisted touch spends the 50, once";
        EXPECT_LT(ghost->stats()->hitpoints(), 100.0f) << "the host struck back";
        EXPECT_EQ(0u, ghost->possess_link()) << "the host resisted";
        EXPECT_FALSE(ghost->hidden());
        EXPECT_EQ(kFoeTeam, orc->team_num());
        EXPECT_EQ(ghost, orc->foe()) << "the host turns on the ghost";
        EXPECT_TRUE(notified(tw, "resisted!"));
    }
    {
        // The landed case: a state where the level roll wins.
        const std::uint32_t lands = state_where([](og::sim::SimRandom& r) {
            const std::uint32_t level_roll = r.next(70);
            return r.next(40) <= level_roll;
        });
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 7, 100.0f);
        walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
        tw.world().rng_.state_ = lands;
        const CastResult r = cast(ghost, 3, false);
        ASSERT_TRUE(r.ok) << r.reason;
        EXPECT_FLOAT_EQ(50.0f, ghost->stats()->magicpoints());
        EXPECT_EQ(orc->entity_id(), ghost->possess_link());
        EXPECT_EQ(ghost->entity_id(), orc->possess_link());
        EXPECT_TRUE(ghost->hidden()) << "the ghost is inside its host";
        EXPECT_EQ(kGhostTeam, orc->team_num()) << "the host fights for the ghost";
        EXPECT_EQ(360, orc->possess_ticks()) << "120 + 40 x the level gap of 6";
        EXPECT_EQ(1, ever_made(tw, Order::FX, FAMILY_GHOST_SCARE))
            << "the entrance burst";
        EXPECT_TRUE(notified(tw, "possesses"));
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// From a level gap of possess_permanent_gap (8) the ride is for good
// (ticks 0); below it the duration follows the gap, never under 60.
//
// Perturbation: possess_permanent_gap = 99 in the staged ghost tuning. RED:
// "a gap of 9 is permanent" (480 ticks, not 0) and "a gap of 8 is
// permanent" (440, not 0).
TEST(KitGhost, large_gap_is_permanent)
{
    og::test::ScopedHookFailureGuard guard;
    struct Row {
        short ghost_level;
        short host_level;
        int ticks;
        const char* what;
    };
    const Row rows[] = {
        {10, 1, 0, "a gap of 9 is permanent"},
        {10, 2, 0, "a gap of 8 is permanent"},
        {10, 3, 400, "a gap of 7: 120 + 40 x 7"},
        {7, 9, 60, "out-levelled: never under possess_min"},
    };
    for (const Row& row : rows) {
        const short gl = row.ghost_level;
        const std::uint32_t lands = state_where([gl](og::sim::SimRandom& r) {
            const std::uint32_t level_roll =
                r.next(static_cast<std::uint32_t>(gl) * 10u);
            return r.next(40) <= level_roll;
        });
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, row.ghost_level, 100.0f);
        walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64,
                                 row.host_level, 140.0f);
        tw.world().rng_.state_ = lands;
        const CastResult r = cast(ghost, 3, false);
        ASSERT_TRUE(r.ok) << r.reason << " " << row.what;
        ASSERT_EQ(orc->entity_id(), ghost->possess_link()) << row.what;
        EXPECT_EQ(row.ticks, orc->possess_ticks()) << row.what;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The refusals come before the roll: none of them spends mana or draws.
// With the setting off the slot is refused for mana.
//
// Perturbation: the host_refusal undead arm deleted in the staged
// kit_ghost.lua. RED: "nothing drawn" on the skeleton row (the RNG state
// 2802067423, not 12345): the engine still says UNDEAD RESIST, but only
// after the resist roll was drawn.
TEST(KitGhost, possess_refusals_spend_nothing_and_draw_nothing)
{
    og::test::ScopedHookFailureGuard guard;
    struct Row {
        const char* reason;
        std::function<void(TestGameWorld&, walker* ghost, walker* host)> stage;
    };
    const Row rows[] = {
        {"NO HOST IN REACH",
         [](TestGameWorld&, walker*, walker* host) { host->setxy(300, 300); }},
        {"HERO RESISTS",
         [](TestGameWorld&, walker*, walker* host) {
             host->set_user(1);
             host->set_act_type(ACT_CONTROL);
         }},
        {"ALREADY POSSESSED",
         [](TestGameWorld&, walker*, walker* host) {
             host->set_possess_link(9999);
         }},
        {"HOST IS CHARMED",
         [](TestGameWorld&, walker*, walker* host) {
             host->set_real_team_num(3);
         }},
        {"CANNOT POSSESS",
         [](TestGameWorld&, walker* ghost, walker*) {
             ghost->set_real_team_num(2);
         }},
        {"PHASED",
         [](TestGameWorld&, walker* ghost, walker*) {
             ghost->stats()->set_bit_flags(BIT_PHANTOM, 1);
         }},
    };
    for (const Row& row : rows) {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 7, 100.0f);
        walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
        row.stage(tw, ghost, orc);
        tw.world().rng_.state_ = 12345u;
        const CastResult r = cast(ghost, 3, false);
        EXPECT_FALSE(r.ok) << row.reason;
        EXPECT_EQ(row.reason, r.reason);
        EXPECT_LE(r.reason.size(), 24u);
        EXPECT_FLOAT_EQ(100.0f, ghost->stats()->magicpoints()) << row.reason;
        EXPECT_EQ(12345u, tw.world().rng_.state_)
            << row.reason << ": nothing drawn";
        EXPECT_FALSE(ghost->hidden()) << row.reason;
    }
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 7, 100.0f);
        add_living(tw, FAMILY_SKELETON, kFoeTeam, 80, 64, 1, 140.0f);
        tw.world().rng_.state_ = 12345u;
        const CastResult r = cast(ghost, 3, false);
        EXPECT_FALSE(r.ok);
        EXPECT_EQ("UNDEAD RESIST", r.reason);
        EXPECT_EQ(12345u, tw.world().rng_.state_) << "nothing drawn";
        EXPECT_FLOAT_EQ(100.0f, ghost->stats()->magicpoints());
    }
    {
        TestGameWorld tw;  // OFF twin
        walker* ghost = add_ghost(tw, 7, 100.0f);
        walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
        tw.world().rng_.state_ = 12345u;
        const CastResult r = cast(ghost, 3, false);
        EXPECT_FALSE(r.ok);
        EXPECT_EQ(Failure::NoMP, r.why) << "off: POSSESS is not in play";
        EXPECT_EQ(12345u, tw.world().rng_.state_);
        EXPECT_EQ(0u, orc->possess_link());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A bot ghost possesses too: through living::act's own special roll, the
// slot pick, the shift coin and the POSSESS gate. The seed is found by
// trying fresh scenes until one possesses; with that same seed a seated
// hero, a skeleton, a foe out of reach and the setting off are each never
// possessed, so the gate is what decides.
//
// Perturbation: ai_possess's last line turned to `self.level >= foe.level
// + 50` in the staged kit_ghost.lua. RED: "no seed in 3000 lets a bot ghost
// possess" (the gate never opens).
TEST(KitGhost, bot_ghost_possesses_an_adjacent_bot_foe_only)
{
    og::test::ScopedHookFailureGuard guard;
    enum class Variant { Plain, Hero, Undead, Far, Off };
    struct Outcome {
        bool possessed = false;
        bool ghost_hidden = false;
        int ticks = -1;
        int host_team = -1;
    };
    const auto run = [](Variant v, std::uint32_t seed) {
        TestGameWorld tw;
        tw.world().new_specials = v == Variant::Off ? 0 : 1;
        walker* ghost = add_ghost(tw, 7, 100.0f);
        ghost->set_act_type(ACT_RANDOM);
        const int family = v == Variant::Undead ? FAMILY_SKELETON : FAMILY_ORC;
        const short x = v == Variant::Far ? 200 : 80;
        walker* host = add_living(tw, family, kFoeTeam, x, 64, 1, 140.0f);
        host->set_act_type(ACT_GUARD);
        if (v == Variant::Hero) {
            host->set_user(1);
            host->set_act_type(ACT_CONTROL);
        }
        tw.world().rng_.state_ = seed;
        dynamic_cast<living*>(ghost)->act();
        Outcome o;
        o.possessed = ghost->possess_link() != 0;
        o.ghost_hidden = ghost->hidden();
        o.ticks = host->possess_ticks();
        o.host_team = host->team_num();
        return o;
    };
    std::uint32_t seed = 0;
    for (std::uint32_t s = 1; s <= 3000 && seed == 0; ++s)
        if (run(Variant::Plain, s).possessed)
            seed = s;
    ASSERT_NE(0u, seed) << "no seed in 3000 lets a bot ghost possess";
    const Outcome plain = run(Variant::Plain, seed);
    EXPECT_TRUE(plain.ghost_hidden);
    EXPECT_EQ(kGhostTeam, plain.host_team) << "the host fights for the ghost";
    EXPECT_EQ(360, plain.ticks);
    EXPECT_FALSE(run(Variant::Hero, seed).possessed) << "never a seated hero";
    EXPECT_FALSE(run(Variant::Undead, seed).possessed) << "never the undead";
    EXPECT_FALSE(run(Variant::Far, seed).possessed) << "only a touching foe";
    EXPECT_FALSE(run(Variant::Off, seed).possessed) << "off: never";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A host that carries a hero record (an AI-driven company hero) resists
// with its own constitution, not its hit points; and a refusal only the
// engine knows (here a ghost already linked) comes back as the cast's
// reason, with the mana kept.
TEST(KitGhost, possess_reads_a_heros_constitution_and_passes_engine_refusals)
{
    og::test::ScopedHookFailureGuard guard;
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 7, 100.0f);
        ghost->set_user(0);
        ghost->set_act_type(ACT_CONTROL);
        walker* hero = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
        auto record = std::make_unique<guy>(FAMILY_ORC);
        record->constitution = 50;  // a roll over 500 beats any level-7 roll
        hero->set_owned_myguy(std::move(record));
        // With hit points (con 4) this state lands; with con 50 it resists.
        const std::uint32_t seed = state_where([](og::sim::SimRandom& rng) {
            const std::uint32_t level_roll = rng.next(70);
            og::sim::SimRandom twin = rng;
            const bool lands_on_hp = twin.next(40) <= level_roll;
            return lands_on_hp && rng.next(500) > level_roll;
        });
        ASSERT_NE(0u, seed);
        tw.world().rng_.state_ = seed;
        const CastResult r = cast(ghost, 3, false);
        EXPECT_TRUE(r.ok) << r.reason;
        EXPECT_EQ(0u, ghost->possess_link()) << "the hero's constitution resisted";
    }
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 7, 100.0f);
        add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
        ghost->set_possess_link(4242);
        tw.world().rng_.state_ = state_where([](og::sim::SimRandom& rng) {
            const std::uint32_t level_roll = rng.next(70);
            return rng.next(40) <= level_roll;
        });
        const CastResult r = cast(ghost, 3, false);
        EXPECT_FALSE(r.ok);
        EXPECT_EQ("CANNOT POSSESS", r.reason) << "the engine's own refusal";
        EXPECT_FLOAT_EQ(100.0f, ghost->stats()->magicpoints());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// PHASE
// ---------------------------------------------------------------------------

// For phase_ticks (48) world ticks the ghost is invulnerable, out of the
// collision table, too busy to swing and twice as fast; a second press is
// refused for free. The tick the veil runs out, everything is put back.
//
// Perturbation: phase_ticks = 1 in the staged ghost tuning. RED: "the
// phase lasts phase_ticks": it ended on tick 1, not 48.
// Perturbation: the kit as it was before the cast set busy itself (the
// veil alone held the ghost, from its first act). RED: "too busy to swing
// from the cast on" (busy 0 vs 2).
TEST(KitGhost, phase_doubles_speed_blocks_attacks_and_expires_clean)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 10, 200.0f);
    ghost->set_act_type(ACT_CONTROL);  // no AI: the test drives the ticks
    walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 300, 300, 1, 140.0f);
    const float normal_step = ghost->stepsize();
    ASSERT_TRUE(in_obmap(tw, ghost));

    const CastResult r = cast(ghost, 4, false);
    ASSERT_TRUE(r.ok) << r.reason;
    EXPECT_FLOAT_EQ(140.0f, ghost->stats()->magicpoints()) << "PHASE costs 60";
    EXPECT_TRUE(ghost->stats()->query_bit_flags(BIT_PHANTOM));
    EXPECT_TRUE(ghost->stats()->query_bit_flags(BIT_NO_COLLIDE));
    EXPECT_FALSE(in_obmap(tw, ghost)) << "out of the collision table at once";
    EXPECT_EQ(1u, live_of(tw, Order::FX, marker_family()).size());
    // A Fire pressed with the PHASE press is handled after it, before the
    // veil first acts: the cast itself makes the ghost too busy to swing
    // (init_fire refuses while busy is above 0).
    EXPECT_GE(ghost->busy(), 2.0f) << "too busy to swing from the cast on";

    const CastResult again = cast(ghost, 4, false);
    EXPECT_FALSE(again.ok);
    EXPECT_EQ("ALREADY PHASED", again.reason);
    EXPECT_FLOAT_EQ(140.0f, ghost->stats()->magicpoints()) << "refused free";
    EXPECT_EQ("PHASED", cast(ghost, 2, false).reason) << "no siphon while phased";

    int ended_on = 0;
    for (int tick = 1; tick <= 60 && ended_on == 0; ++tick) {
        tw.world().tick();
        if (!ghost->stats()->query_bit_flags(BIT_PHANTOM)) {
            ended_on = tick;
            break;
        }
        EXPECT_GE(ghost->busy(), 1.0f) << "tick " << tick;
        EXPECT_FALSE(ghost->init_fire()) << "tick " << tick << ": no swing";
        EXPECT_GT(ghost->invulnerable_left(), 0) << "tick " << tick;
        EXPECT_FALSE(orc->attack(ghost)) << "tick " << tick;
        EXPECT_FALSE(in_obmap(tw, ghost)) << "tick " << tick;
        EXPECT_FLOAT_EQ(2.0f * normal_step, ghost->stepsize())
            << "tick " << tick << ": double speed";
        EXPECT_TRUE(ghost->stats()->query_bit_flags(BIT_PHANTOM))
            << "tick " << tick << ": still phased";
    }
    EXPECT_EQ(48, ended_on) << "the phase lasts phase_ticks";
    EXPECT_FALSE(ghost->stats()->query_bit_flags(BIT_NO_COLLIDE));
    EXPECT_FALSE(ghost->ignore());
    EXPECT_TRUE(in_obmap(tw, ghost)) << "back in the collision table";
    EXPECT_EQ(0, ghost->invulnerable_left());
    EXPECT_EQ(0, ghost->speed_bonus_left());
    EXPECT_TRUE(live_of(tw, Order::FX, marker_family()).empty());
    tw.world().tick();
    EXPECT_FLOAT_EQ(normal_step, ghost->stepsize()) << "normal speed again";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// PHASE OFF twin, and the veil's own exits: a veil whose ghost is gone dies.
TEST(KitGhost, phase_is_not_in_play_when_off_and_an_orphan_veil_dies)
{
    og::test::ScopedHookFailureGuard guard;
    {
        TestGameWorld tw;
        walker* ghost = add_ghost(tw, 10, 100.0f);
        const CastResult r = cast(ghost, 4, false);
        EXPECT_FALSE(r.ok);
        EXPECT_EQ(Failure::NoMP, r.why) << "off: PHASE is not in play";
        EXPECT_FALSE(ghost->stats()->query_bit_flags(BIT_PHANTOM));
        EXPECT_TRUE(live_of(tw, Order::FX, marker_family()).empty());
    }
    {
        TestGameWorld tw;
        tw.world().new_specials = 1;
        walker* ghost = add_ghost(tw, 10, 100.0f);
        ASSERT_TRUE(cast(ghost, 4, false).ok);
        const std::vector<walker*> veils = live_of(tw, Order::FX, marker_family());
        ASSERT_EQ(1u, veils.size());
        ghost->set_dead(1);
        veils[0]->act();
        EXPECT_TRUE(veils[0]->dead()) << "a veil with no ghost dies";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// Bot gates
// ---------------------------------------------------------------------------

namespace {

bool gate(walker* w, int slot)
{
    w->set_current_special(static_cast<char>(slot));
    return og::test::check_special_ai(*get_family_descriptor(FAMILY_GHOST),
                                      dynamic_cast<living*>(w));
}

}  // namespace

// SIPHON: a hurt ghost (under 80 %) with a foe touching it.
TEST(KitGhost, ai_siphon_fires_when_hurt_and_a_foe_is_touching)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 4, 40.0f);
    walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 1, 140.0f);
    ghost->stats()->set_hitpoints(70.0f);
    EXPECT_TRUE(gate(ghost, 2)) << "hurt, foe touching";
    ghost->stats()->set_hitpoints(90.0f);
    EXPECT_FALSE(gate(ghost, 2)) << "not hurt enough";
    ghost->stats()->set_hitpoints(70.0f);
    orc->setxy(200, 64);
    EXPECT_FALSE(gate(ghost, 2)) << "no foe touching";
    orc->setxy(80, 64);
    ghost->stats()->set_bit_flags(BIT_PHANTOM, 1);
    EXPECT_FALSE(gate(ghost, 2)) << "a phased ghost cannot touch";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// POSSESS: a living, unseated, unlinked, uncharmed, not-undead foe touching
// a ghost of at least its level.
TEST(KitGhost, ai_possess_fires_when_a_weaker_bot_foe_is_touching)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 5, 100.0f);
    walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 80, 64, 3, 140.0f);
    EXPECT_TRUE(gate(ghost, 3)) << "a weaker orc touching";
    orc->stats()->set_level(6);
    EXPECT_FALSE(gate(ghost, 3)) << "the orc out-levels the ghost";
    orc->stats()->set_level(5);
    EXPECT_TRUE(gate(ghost, 3)) << "an equal level will do";
    orc->set_user(1);
    EXPECT_FALSE(gate(ghost, 3)) << "a seated hero";
    orc->set_user(-1);
    orc->setxy(200, 64);
    EXPECT_FALSE(gate(ghost, 3)) << "out of reach";
    orc->setxy(80, 64);
    ghost->set_real_team_num(2);
    EXPECT_FALSE(gate(ghost, 3)) << "a charmed ghost";
    ghost->set_real_team_num(255);
    ghost->stats()->set_bit_flags(BIT_PHANTOM, 1);
    EXPECT_FALSE(gate(ghost, 3)) << "a phased ghost";
    ghost->stats()->set_bit_flags(BIT_PHANTOM, 0);
    orc->setxy(300, 300);
    add_living(tw, FAMILY_SKELETON, kFoeTeam, 80, 64, 1, 100.0f);
    EXPECT_FALSE(gate(ghost, 3)) << "the undead";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// PHASE: a badly hurt ghost (under 30 %) with a foe within 48 px, and not
// while a veil already holds it.
// Perturbation: the gate's veil check removed. RED: "already phased: no
// roll for a cast that would be refused".
TEST(KitGhost, ai_phase_fires_when_low_and_a_foe_is_close)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    walker* ghost = add_ghost(tw, 10, 100.0f);
    walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 100, 64, 1, 140.0f);
    ghost->stats()->set_hitpoints(20.0f);
    EXPECT_TRUE(gate(ghost, 4)) << "low and a foe close";
    ghost->stats()->set_hitpoints(50.0f);
    EXPECT_FALSE(gate(ghost, 4)) << "not low enough";
    ghost->stats()->set_hitpoints(20.0f);
    orc->setxy(300, 64);
    EXPECT_FALSE(gate(ghost, 4)) << "no foe close";
    orc->setxy(100, 64);
    ASSERT_TRUE(gate(ghost, 4));
    ASSERT_TRUE(cast(ghost, 4, false).ok);
    EXPECT_FALSE(gate(ghost, 4))
        << "already phased: no roll for a cast that would be refused";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// With the setting off every new gate answers true on its first statement:
// no foe search, no draw, no change to the walker (the engine answers the
// same "no hook -> true" for a slot no family declares). SCARE's gate is
// the classic one either way.
//
// Perturbation: the setting guard deleted from ai_siphon in the staged
// kit_ghost.lua. RED: "off: the new gate answers true at once (slot 2)"
// (the full-health ghost's gate answered false).
TEST(KitGhost, ai_off_answers_the_classic_gate)
{
    og::test::ScopedHookFailureGuard guard;
    for (const int slot : {2, 3, 4}) {
        TestGameWorld tw;  // OFF
        walker* ghost = add_ghost(tw, 1, 40.0f);  // full health, level 1
        walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 300, 300, 9, 140.0f);
        tw.world().rng_.state_ = 777u;
        EXPECT_TRUE(gate(ghost, slot))
            << "off: the new gate answers true at once (slot " << slot << ")";
        EXPECT_EQ(777u, tw.world().rng_.state_) << "slot " << slot;
        EXPECT_EQ(nullptr, ghost->foe()) << "slot " << slot;
        (void)orc;
    }
    for (const short flag : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().new_specials = flag;
        walker* ghost = add_ghost(tw, 1, 40.0f);
        walker* orc = add_living(tw, FAMILY_ORC, kFoeTeam, 100, 64, 1, 140.0f);
        EXPECT_TRUE(gate(ghost, 1)) << "SCARE: a foe within 130, flag " << flag;
        orc->setxy(600, 600);
        ghost->set_foe(nullptr);
        EXPECT_FALSE(gate(ghost, 1)) << "SCARE: no foe near, flag " << flag;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// Art and glyphs
// ---------------------------------------------------------------------------

// core:wail is effect wire 14 and wears the scare's sparkle cloud (eight
// frames, a dot growing to a ring), with the scare's magenta bold, as `*`,
// in the terminal clients.
//
// Perturbation: sprite = "lightnin.png" in the staged effect-14-wail.lua.
// RED: pix_filename "lightnin.png", not "expand8.png".
TEST(KitGhost, new_kit_entities_load_art_and_glyphs)
{
    ASSERT_EQ(14, wail_family()) << "core:wail is effect wire id 14";
    const EffectFamilyDescriptor* d = get_effect_family_descriptor(14);
    ASSERT_NE(nullptr, d);
    ASSERT_NE(nullptr, d->pix_filename);
    EXPECT_EQ(std::string("expand8.png"), d->pix_filename);
    const PixieData p = read_pixie_file(d->pix_filename);
    ASSERT_TRUE(p.valid());
    EXPECT_EQ(8, static_cast<int>(p.frames)) << "the scare's eight frames";
    EXPECT_EQ(U'*', d->glyph.codepoint);
    EXPECT_EQ('*', d->glyph.ascii);
    EXPECT_EQ(og::GlyphColor::Magenta, d->glyph.color);
    EXPECT_TRUE(d->glyph.bold);
    EXPECT_FALSE(d->glyph.transparent) << "the bolt is seen on every client";
}
