/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch libs-2: the rules the review found in effect_ghost_scare.lua, effect_hook_blade.lua, effect_knife_back.lua, effect_mine.lua, effect_shield.lua, effect_wail.lua, gates.lua, kit_captain.lua.
// Each test is named after the rule it asserts.
//
// Every test was proven once against a planted break of its rule in the
// shipped Lua (packs/core/lib/..., restaged with --target
// stage_runtime_assets, then reverted); the break and the line it failed on
// are in the batch report, and each test names its break in a comment.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "../test_sim_random_scope.h"
#include "bot_gate_fixture.h"

#include <openglad/core/combat_math.h>
#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/effect.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {

// ---------------------------------------------------------------- helpers

// A 32x32-tile grass field (512 x 512 px), nothing else on it.
void open_field(TestGameWorld& tw)
{
    GameWorld& world = tw.world();
    world.resize_grid(32, 32);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.mysmoother.set_target(world.grid);
    world.my_team = 0;
}

// A living of `family` on `team` with its top-left at (x, y): 200 hp, no
// armour, idle.
walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y, short level = 1)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(x, y);
    w->stats()->set_level(level);
    w->stats()->set_max_hitpoints(200.0f);
    w->stats()->set_hitpoints(200.0f);
    w->stats()->set_armor(0.0f);
    w->set_busy(0);
    return w;
}

walker* add_generator(TestGameWorld& tw, unsigned char team, short x, short y)
{
    walker* w = tw.world().add_ob(Order::Generator, FAMILY_TENT);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->setxy(x, y);
    w->stats()->set_max_hitpoints(200.0f);
    w->stats()->set_hitpoints(200.0f);
    return w;
}

// Makes `w` a hero (it carries a guy) of constitution `con`.
void make_hero(walker* w, short con)
{
    w->set_owned_myguy(std::make_unique<guy>(static_cast<int>(w->family())));
    w->myguy->constitution = con;
}

const command* front_command(const walker* w)
{
    if (w->stats()->commands.empty())
        return nullptr;
    return &w->stats()->commands.front();
}

int frozen(const walker* w)
{
    return static_cast<int>(w->stats()->frozen_delay());
}

int count_sound(const TestGameWorld& tw, std::uint32_t sound)
{
    int n = 0;
    for (const auto& e : tw.events.events())
        if (e.kind == og::sim::EventKind::PlaySound && e.a == sound)
            ++n;
    return n;
}

int fx_family(const char* id)
{
    return og::families::resolve_family_string_id(Order::FX, id);
}

int weapon_family(const char* id)
{
    return og::families::resolve_family_string_id(Order::Weapon, id);
}

std::vector<walker*> live_of(const std::list<std::unique_ptr<walker>>& list,
                             Order order, int family)
{
    std::vector<walker*> out;
    for (const auto& uptr : list)
        if (uptr && !uptr->dead() && uptr->query_order() == order &&
            uptr->family() == family)
            out.push_back(uptr.get());
    return out;
}

std::vector<walker*> all_of(const std::list<std::unique_ptr<walker>>& list,
                            Order order, int family)
{
    std::vector<walker*> out;
    for (const auto& uptr : list)
        if (uptr && uptr->query_order() == order && uptr->family() == family)
            out.push_back(uptr.get());
    return out;
}

// The world-RNG state after exactly the draws `bounds` from `state`.
std::uint32_t state_after(std::uint32_t state,
                          const std::vector<std::uint32_t>& bounds)
{
    og::sim::SimRandom probe(state);
    for (std::uint32_t b : bounds)
        (void)probe.next(b);
    return probe.state_;
}

// The first state from which the draws satisfy `want`.
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

// ===========================================================================
// effect_ghost_scare.lua — the ghost's SCARE cloud (classic: effect.cpp
// effect::death FAMILY_GHOST_SCARE in the old game; same tests, same order)
// ===========================================================================

namespace {

// A level-`level` ghost on team 1 at (160,160) and its scare cloud on it.
struct ScareScene {
    TestGameWorld tw;
    walker* ghost = nullptr;
    walker* scare = nullptr;

    explicit ScareScene(short level)
    {
        og::test::mount_core_pack();
        open_field(tw);
        ghost = add_living(tw, FAMILY_GHOST, 1, 160, 160, level);
    }

    // Spawns the cloud on the ghost and lets it expire (its death frightens).
    bool expire()
    {
        scare = tw.world().add_ob(Order::FX, FAMILY_GHOST_SCARE);
        if (scare == nullptr)
            return false;
        scare->set_owner(ghost);
        scare->setxy(ghost->xpos(), ghost->ypos());
        scare->set_dead(1);
        return scare->death();
    }
};

}  // namespace

// effect_ghost_scare.lua:43. The scare frightens living foes only: an enemy
// tent inside the radius is a foe the finder returns, but it is never handed
// a fright walk, while the soldier beside it is (the control that proves the
// scare went off).
// Break: `foe and foe:order() == C.ORDER_LIVING` -> `foe` (line 43).
TEST(SemGhostScare, a_scare_frightens_living_foes_never_a_generator)
{
    og::test::ScopedHookFailureGuard guard;
    ScareScene s(5);
    ASSERT_NE(nullptr, s.ghost);
    walker* soldier = add_living(s.tw, FAMILY_SOLDIER, 2, 192, 160);
    walker* tent = add_generator(s.tw, 2, 160, 192);
    ASSERT_NE(nullptr, soldier);
    ASSERT_NE(nullptr, tent);
    soldier->stats()->clear_command();
    tent->stats()->clear_command();
    ASSERT_TRUE(s.expire());

    const command* walk = front_command(soldier);
    ASSERT_NE(nullptr, walk) << "living foe in radius: frightened";
    EXPECT_EQ(COMMAND_WALK, walk->commandtype);
    EXPECT_TRUE(walk->forced);
    EXPECT_EQ(125, walk->commandcount) << "living foe: the L5 duration";
    EXPECT_EQ(1, walk->com1) << "living foe: flees east, away from the cloud";
    EXPECT_EQ(0, walk->com2);

    EXPECT_FALSE(tent->stats()->has_commands())
        << "non-living foe (generator): untouched";
    EXPECT_EQ(160, tent->xpos()) << "non-living foe (generator): not moved";
    EXPECT_EQ(192, tent->ypos());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_ghost_scare.lua:50,54. A level-1 scare lasts 25 ticks. A monster
// takes all 25 and the scare draws nothing for it; a hero rolls rand0(con)
// off the fright (one draw); a hero whose roll eats the whole fright does
// not flee at all, though the draw was made.
// Breaks: `if foe:has_guy() then` -> `if false then` (hero arm red);
// `if fright > 0 then` -> `if fright > -1000 then` (full-resist arm red);
// `og.rand0(7)` added above the has_guy test (monster no-draw arm red).
TEST(SemGhostScare, a_hero_resists_a_scare_by_a_constitution_roll_a_monster_never_rolls)
{
    og::test::ScopedHookFailureGuard guard;
    ASSERT_EQ(25, og::combat::scare_duration(1));
    {
        ScareScene s(1);
        walker* monster = add_living(s.tw, FAMILY_ORC, 2, 192, 160);
        ASSERT_NE(nullptr, monster);
        monster->stats()->clear_command();
        s.tw.world().rng_.state_ = 4242u;
        ASSERT_TRUE(s.expire());
        const command* walk = front_command(monster);
        ASSERT_NE(nullptr, walk) << "monster foe: frightened";
        EXPECT_EQ(25, walk->commandcount)
            << "monster foe: fright = scare_duration(1)";
        EXPECT_EQ(4242u, s.tw.world().rng_.state_)
            << "monster foe (RNG): no rand0 draw";
    }
    {
        // A roll of 7 out of a constitution of 20.
        ScareScene s(1);
        walker* hero = add_living(s.tw, FAMILY_SOLDIER, 2, 192, 160);
        ASSERT_NE(nullptr, hero);
        make_hero(hero, 20);
        hero->stats()->clear_command();
        const std::uint32_t pin =
            state_where([](og::sim::SimRandom& r) { return r.next(20) == 7; });
        ASSERT_NE(0u, pin);
        s.tw.world().rng_.state_ = pin;
        ASSERT_TRUE(s.expire());
        const command* walk = front_command(hero);
        ASSERT_NE(nullptr, walk) << "hero foe: still frightened";
        EXPECT_EQ(25 - 7, walk->commandcount)
            << "hero foe: the constitution roll ticks off the fright";
        EXPECT_EQ(state_after(pin, {20}), s.tw.world().rng_.state_)
            << "hero foe: exactly one rand0(constitution) draw";
    }
    {
        // A roll of 25 or more out of a constitution of 200.
        ScareScene s(1);
        walker* hero = add_living(s.tw, FAMILY_SOLDIER, 2, 192, 160);
        ASSERT_NE(nullptr, hero);
        make_hero(hero, 200);
        hero->stats()->clear_command();
        const std::uint32_t pin =
            state_where([](og::sim::SimRandom& r) { return r.next(200) >= 25; });
        ASSERT_NE(0u, pin);
        s.tw.world().rng_.state_ = pin;
        ASSERT_TRUE(s.expire());
        EXPECT_FALSE(hero->stats()->has_commands())
            << "fright <= 0 after the roll: no command queued";
        EXPECT_EQ(state_after(pin, {200}), s.tw.world().rng_.state_)
            << "the resisting hero's roll was still drawn";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_knife_back.lua — the soldier's returning knife (classic: effect.cpp
// effect::act FAMILY_KNIFE_BACK in the old game, the same branches)
// ===========================================================================

namespace {

// A team-1 soldier (the thrower, 0 knives left) at (tx, ty) and its returning
// blade at (bx, by), damage 40, flying at the knife's 5 px step.
struct KnifeScene {
    TestGameWorld tw;
    const EffectFamilyDescriptor* efd = nullptr;
    walker* thrower = nullptr;
    walker* blade = nullptr;

    KnifeScene(short tx, short ty, short bx, short by, unsigned char team = 1)
    {
        og::test::mount_core_pack();
        open_field(tw);
        efd = get_effect_family_descriptor(FAMILY_KNIFE_BACK);
        thrower = add_living(tw, FAMILY_SOLDIER, team, tx, ty);
        blade = tw.world().add_fx_ob(Order::FX, FAMILY_KNIFE_BACK);
        if (thrower == nullptr || blade == nullptr)
            return;
        thrower->set_weapons_left(0);
        blade->set_owner(thrower);
        blade->set_team_num(team);
        blade->setxy(bx, by);
        blade->set_damage(40.0f);
        // weapon_knife.lua hands the blade the thrown knife's step.
        blade->set_stepsize(5.0f);
        blade->set_ani_type(static_cast<char>(ANI_ATTACK));
    }

    bool act() { return og::test::on_act(*efd, static_cast<effect*>(blade)); }

    int knives() const
    {
        return static_cast<int>(
            all_of(tw.world().weaplist, Order::Weapon, FAMILY_KNIFE).size());
    }
};

}  // namespace

// effect_knife_back.lua:12,17. A blade whose thrower lives and is more than
// 10 px off (Manhattan, corner to corner) flies one step home and refunds
// nothing; at 11 px it still flies, at 10 px it is caught.
// Breaks: `if not owner or owner:dead() ~= 0 then` -> `if true then`
// (alive arm red); `if distance > 10 then` -> `if distance > 11 then`
// (the 11 px row red).
TEST(SemKnifeBack, a_returning_knife_flies_home_until_it_is_within_10_px)
{
    og::test::ScopedHookFailureGuard guard;
    {
        KnifeScene s(100, 100, 200, 100);
        ASSERT_NE(nullptr, s.blade);
        ASSERT_NE(nullptr, s.efd);
        const float step = s.blade->stepsize();
        ASSERT_GT(step, 1.0f) << "the blade flies at its table step";
        ASSERT_TRUE(s.act());
        EXPECT_EQ(0, s.blade->dead()) << "alive thrower: the blade flies home";
        EXPECT_FLOAT_EQ(200.0f - step, s.blade->worldx())
            << "distance > 10: one step toward the thrower";
        EXPECT_FLOAT_EQ(100.0f, s.blade->worldy());
        EXPECT_EQ(ANI_ATTACK, static_cast<int>(s.blade->ani_type()));
        EXPECT_EQ(0, s.thrower->weapons_left())
            << "distance > 10: weapons_left unchanged";
    }
    {
        KnifeScene s(100, 100, 106, 105);
        ASSERT_NE(nullptr, s.blade);
        ASSERT_EQ(11, s.blade->distance_to_ob(s.thrower));
        ASSERT_TRUE(s.act());
        EXPECT_EQ(0, s.blade->dead()) << "11 px: not caught";
        EXPECT_EQ(0, s.thrower->weapons_left()) << "11 px: no refund";
    }
    {
        KnifeScene s(100, 100, 105, 105);
        ASSERT_NE(nullptr, s.blade);
        ASSERT_EQ(10, s.blade->distance_to_ob(s.thrower));
        ASSERT_TRUE(s.act());
        EXPECT_NE(0, s.blade->dead()) << "10 px: caught";
        EXPECT_EQ(1, s.thrower->weapons_left()) << "10 px: the throw comes back";
        EXPECT_EQ(ANI_WALK, static_cast<int>(s.blade->ani_type()));
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_knife_back.lua:19,21,24,26. Each axis steps at most the blade's
// stepsize toward the thrower's corner and never past it; an axis already
// level with the thrower does not move.
// Breaks: `xd = og.min(self:stepsize(), owner:xpos() - self:xpos())` and the
// y twin -> `... = self:stepsize()` (right/below arm red); `og.max(owner:ypos()
// - self:ypos(), -self:stepsize())` and the x twin -> `-self:stepsize()`
// (left/above arm red); `local xd, yd = 0, 0` -> `local xd, yd = 0, 1`
// (aligned arm red).
TEST(SemKnifeBack, a_returning_knife_steps_at_most_its_stepsize_per_axis_and_never_overshoots)
{
    og::test::ScopedHookFailureGuard guard;
    float s = 0.0f;
    {
        KnifeScene probe(100, 100, 200, 200);
        ASSERT_NE(nullptr, probe.blade);
        s = probe.blade->stepsize();
    }
    ASSERT_GT(s, 3.0f) << "the gaps below are smaller than a step";
    // Blade at (200,200) every time.
    const struct {
        short tx, ty;
        float dx, dy;
        const char* arm;
    } cases[] = {
        {300, 202, s, 2.0f, "thrower right/below: +step, and +2 (the gap) on y"},
        {202, 300, 2.0f, s, "thrower right/below: +2 (the gap) on x, +step on y"},
        {100, 198, -s, -2.0f, "thrower left/above: -step, and -2 (the gap) on y"},
        {198, 100, -2.0f, -s, "thrower left/above: -2 (the gap) on x, -step on y"},
        {100, 200, -s, 0.0f, "aligned y: y does not move"},
        {200, 300, 0.0f, s, "aligned x: x does not move"},
    };
    for (const auto& c : cases) {
        KnifeScene k(c.tx, c.ty, 200, 200);
        ASSERT_NE(nullptr, k.blade);
        ASSERT_TRUE(k.act()) << c.arm;
        ASSERT_EQ(0, k.blade->dead()) << c.arm;
        EXPECT_FLOAT_EQ(200.0f + c.dx, k.blade->worldx()) << c.arm;
        EXPECT_FLOAT_EQ(200.0f + c.dy, k.blade->worldy()) << c.arm;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_knife_back.lua:32. Each step home is probed by a throwaway knife:
// with room in the world the probe is made (on the blade's team, floor and
// damage, owned by the thrower, its own death already spent) and put down
// at once; in a full world the blade drops out of the air, refunding
// nothing.
// Breaks: `if not probe then` -> `if probe then` (both arms red);
// `self:set_dead(1)` in that branch deleted (probe-failed arm red).
TEST(SemKnifeBack, a_returning_knife_that_cannot_make_its_probe_drops_without_a_refund)
{
    og::test::ScopedHookFailureGuard guard;
    {
        KnifeScene s(100, 100, 200, 100);
        ASSERT_NE(nullptr, s.blade);
        s.tw.world().set_floor_count(2);
        s.thrower->set_floor(1);
        s.blade->set_floor(1);
        const int before = s.knives();
        ASSERT_TRUE(s.act());
        EXPECT_EQ(0, s.blade->dead()) << "probe spawned: the blade flies on";
        const auto probes = all_of(s.tw.world().weaplist, Order::Weapon, FAMILY_KNIFE);
        ASSERT_EQ(static_cast<std::size_t>(before + 1), probes.size())
            << "probe spawned: one collision probe";
        walker* probe = probes.back();
        EXPECT_NE(0, probe->dead()) << "the probe is put down at once";
        EXPECT_EQ(1, probe->death_called()) << "it never spawns a knife_back";
        EXPECT_EQ(s.thrower, probe->owner());
        EXPECT_EQ(s.blade->team_num(), probe->team_num());
        EXPECT_EQ(1, probe->floor()) << "on the blade's floor";
        EXPECT_FLOAT_EQ(40.0f, probe->damage()) << "at the blade's damage";
        EXPECT_FLOAT_EQ(s.blade->worldx(), probe->worldx());
    }
    {
        KnifeScene s(100, 100, 200, 100);
        ASSERT_NE(nullptr, s.blade);
        const auto factory = s.tw.world().entity_factory;
        s.tw.world().entity_factory = [factory](Order order, std::int32_t family) {
            if (order == Order::Weapon)
                return std::unique_ptr<walker>();
            return factory(order, family);
        };
        const std::size_t weapons_before = s.tw.world().weaplist.size();
        ASSERT_TRUE(s.act());
        s.tw.world().entity_factory = factory;
        EXPECT_NE(0, s.blade->dead()) << "probe spawn failed: the blade drops";
        EXPECT_EQ(ANI_WALK, static_cast<int>(s.blade->ani_type()));
        EXPECT_EQ(0, s.thrower->weapons_left())
            << "probe spawn failed: weapons_left unchanged";
        EXPECT_EQ(weapons_before, s.tw.world().weaplist.size());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_knife_back.lua:45. A returning knife still cuts what stands in its
// next step: the probe strikes it at the blade's damage (the hit a damage-40
// knife lands on an unarmoured soldier) and the blade's damage is quartered,
// 40 -> 10; with nothing in the way nothing is struck and the damage holds.
// Breaks: `if not og.query_object_passable(...)` -> `if false` (object arm
// red); `og.fdiv(self:damage(), 4.0)` -> `og.fdiv(self:damage(), 1.0)`
// (quartering red).
TEST(SemKnifeBack, a_returning_knife_cuts_what_it_passes_and_quarters_its_damage)
{
    og::test::ScopedHookFailureGuard guard;
    {
        KnifeScene s(100, 100, 160, 100, 0);
        ASSERT_NE(nullptr, s.blade);
        walker* enemy = add_living(s.tw, FAMILY_SOLDIER, 1, 140, 100);
        ASSERT_NE(nullptr, enemy);
        FixedRandom zero(0);
        const short want_hit = damage_to_hit_points(
            compute_base_damage(40.0f, zero) -
            compute_damage_reduction(compute_base_damage(40.0f, zero), 0.0f));
        ASSERT_GT(want_hit, 0);
        ASSERT_TRUE(s.act());
        EXPECT_FLOAT_EQ(200.0f - static_cast<float>(want_hit),
                        enemy->stats()->hitpoints())
            << "an object in the next step: struck at the blade's damage";
        EXPECT_FLOAT_EQ(10.0f, s.blade->damage())
            << "an object in the next step: blade damage / 4";
        EXPECT_EQ(0, s.blade->dead()) << "the blade flies on home";
    }
    {
        KnifeScene s(100, 100, 160, 100, 0);
        ASSERT_NE(nullptr, s.blade);
        walker* bystander = add_living(s.tw, FAMILY_SOLDIER, 1, 140, 300);
        ASSERT_NE(nullptr, bystander);
        ASSERT_TRUE(s.act());
        EXPECT_FLOAT_EQ(40.0f, s.blade->damage()) << "clear: damage unchanged";
        EXPECT_FLOAT_EQ(200.0f, bystander->stats()->hitpoints())
            << "clear: no attack";
        EXPECT_FLOAT_EQ(200.0f, s.thrower->stats()->hitpoints());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_shield.lua — the magic shield and the boomerang (classic: effect.cpp
// effect::act FAMILY_MAGIC_SHIELD / FAMILY_BOOMERANG in the old game: the same
// `(hitpoints <= 0) || (lifetime-- < 0)` and `!owner || owner->dead ||
// drawcycle > 253` tests)
// ===========================================================================

namespace {

walker* add_guard_fx(TestGameWorld& tw, int family, walker* owner, short lifetime)
{
    walker* fx = tw.world().add_fx_ob(Order::FX, family);
    if (fx == nullptr)
        return nullptr;
    fx->setxy(100, 100);
    fx->set_owner(owner);
    if (owner != nullptr)
        fx->set_team_num(owner->team_num());
    fx->set_lifetime(lifetime);
    fx->stats()->set_hitpoints(100.0f);
    return fx;
}

}  // namespace

// effect_shield.lua:47,52. A guard with hp left ages one tick an act and
// expires on the act AFTER its count passes 0 (the old count is read before
// the decrement): a shield set to lifetime 0 lives one more act at -1 and is
// gone on the next; lifetime 1 gives three acts in all (classic off-by-one).
// Break: `expired = lifetime < 0` -> `expired = lifetime < -1` (red: alive on
// the act it should die).
TEST(SemShield, classic_a_guard_expires_on_the_act_after_its_count_passes_zero)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    {
        TestGameWorld tw;
        open_field(tw);
        walker* owner = add_living(tw, FAMILY_SOLDIER, 0, 300, 300);
        walker* shield = add_guard_fx(tw, FAMILY_MAGIC_SHIELD, owner, 0);
        ASSERT_NE(nullptr, shield);
        ASSERT_TRUE(shield->act());
        EXPECT_EQ(0, shield->dead()) << "hp > 0 and old lifetime 0: alive";
        EXPECT_EQ(-1, shield->lifetime()) << "aged to -1";
        ASSERT_TRUE(shield->act());
        EXPECT_EQ(1, shield->dead()) << "hp > 0 and old lifetime < 0: dead";
    }
    {
        TestGameWorld tw;
        open_field(tw);
        walker* owner = add_living(tw, FAMILY_SOLDIER, 0, 300, 300);
        walker* shield = add_guard_fx(tw, FAMILY_MAGIC_SHIELD, owner, 1);
        ASSERT_NE(nullptr, shield);
        int acts = 0;
        while (shield->dead() == 0 && acts < 10) {
            shield->act();
            ++acts;
        }
        EXPECT_EQ(3, acts) << "lifetime 1: alive at 0 and at -1, gone on the third";
        EXPECT_FLOAT_EQ(100.0f, shield->stats()->hitpoints()) << "no foe touched it";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// effect_shield.lua:82-84. A boomerang falls with its thrower: no owner, or a
// dead one, ends it on the act (the Lua's own test, dispatched directly; the
// C++ effect::act also lets a dead owner go first). A live owner keeps it
// orbiting.
// Break: `or owner:dead() ~= 0` -> `or owner:dead() ~= owner:dead()`
// (dead-owner arm red); `if not owner` -> `if false` (no-owner arm red: a
// script error on the nil owner).
TEST(SemShield, classic_a_boomerang_dies_with_its_thrower)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    const EffectFamilyDescriptor* efd = get_effect_family_descriptor(FAMILY_BOOMERANG);
    ASSERT_NE(nullptr, efd);
    TestGameWorld tw;
    open_field(tw);
    walker* owner = add_living(tw, FAMILY_SOLDIER, 0, 300, 300);
    walker* fallen = add_living(tw, FAMILY_SOLDIER, 0, 300, 400);
    ASSERT_NE(nullptr, fallen);
    fallen->set_dead(1);

    walker* kept = add_guard_fx(tw, FAMILY_BOOMERANG, owner, 100);
    walker* orphan = add_guard_fx(tw, FAMILY_BOOMERANG, nullptr, 100);
    walker* widow = add_guard_fx(tw, FAMILY_BOOMERANG, fallen, 100);
    ASSERT_NE(nullptr, widow);
    for (walker* b : {kept, orphan, widow})
        b->set_drawcycle(10);
    ASSERT_TRUE(og::test::on_act(*efd, static_cast<effect*>(kept)));
    ASSERT_TRUE(og::test::on_act(*efd, static_cast<effect*>(orphan)));
    ASSERT_TRUE(og::test::on_act(*efd, static_cast<effect*>(widow)));
    EXPECT_EQ(0, kept->dead()) << "live owner: orbits on";
    EXPECT_EQ(99, kept->lifetime()) << "live owner: aged one act";
    EXPECT_EQ(1, orphan->dead()) << "owner gone: dead";
    EXPECT_EQ(1, widow->dead()) << "owner dead: dead";
    EXPECT_EQ(100, widow->lifetime()) << "owner dead: died before ageing";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ===========================================================================
// effect_hook_blade.lua + kit_captain.lua — the orc captain's HOOK BLADE,
// WAR BANNER and WARBAND (New Specials; with the setting off the captain has
// no specials at all, so no blade, banner or grunt can exist)
// ===========================================================================

namespace {

constexpr unsigned char kUs = 0;
constexpr unsigned char kThem = 1;
constexpr int kHookTicks = 48;
constexpr int kHookHp = 30;
constexpr int kHookStart = 12;
constexpr int kHookGrowth = 2;
constexpr int kHookReach = 60;
constexpr int kHookReelMax = 8;
constexpr int kHookStun = 12;

constexpr std::array<int, 16> kOrbitX = {0,  -9, -17, -22, -24, -22, -17, -9,
                                         0,  9,  17,  22,  24,  22,  17,  9};
constexpr std::array<int, 16> kOrbitY = {-24, -22, -17, -9, 0,  9,  17, 22,
                                         24,  22,  17,  9,  0,  -9, -17, -22};

int hook_radius_at(int tick)
{
    const int turn = (kHookReach - kHookStart) / kHookGrowth;
    if (tick <= turn)
        return kHookStart + kHookGrowth * tick;
    return kHookReach - kHookGrowth * (tick - turn);
}

std::pair<float, float> hook_offset_at(int tick)
{
    const std::size_t idx = static_cast<std::size_t>(tick % 16);
    const float r = static_cast<float>(hook_radius_at(tick));
    return {static_cast<float>(kOrbitX[idx]) * r / 24.0f,
            static_cast<float>(kOrbitY[idx]) * r / 24.0f};
}

walker* add_captain(TestGameWorld& tw, short x, short y, short level = 10)
{
    walker* c = add_living(tw, FAMILY_BIG_ORC, kUs, x, y, level);
    if (c == nullptr)
        return nullptr;
    c->stats()->set_max_magicpoints(500.0f);
    c->stats()->set_magicpoints(500.0f);
    return c;
}

struct Cast {
    bool ok = false;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
};

Cast cast(walker* w, int slot, bool shifted)
{
    w->set_current_special(static_cast<char>(slot));
    w->set_shifter_down(shifted ? 1 : 0);
    Cast out;
    out.ok = w->special(&out.why, &out.reason);
    w->set_shifter_down(0);
    return out;
}

std::vector<walker*> blades(TestGameWorld& tw)
{
    return live_of(tw.world().oblist, Order::FX, fx_family("core:hook_blade"));
}

std::vector<walker*> banners(TestGameWorld& tw)
{
    return live_of(tw.world().weaplist, Order::Weapon,
                   weapon_family("core:war_banner"));
}

std::vector<walker*> grunts_of(TestGameWorld& tw, walker* captain)
{
    std::vector<walker*> out;
    for (walker* w : live_of(tw.world().oblist, Order::Living, FAMILY_ORC))
        if (w->owner() == captain)
            out.push_back(w);
    return out;
}

std::vector<short> xs_of(const std::vector<walker*>& ws)
{
    std::vector<short> out;
    for (const walker* w : ws)
        out.push_back(w->xpos());
    return out;
}

walker* add_stain(TestGameWorld& tw, walker* under)
{
    walker* stain = tw.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (stain == nullptr)
        return nullptr;
    stain->set_team_num(kThem);
    stain->set_dead(0);
    stain->set_floor(under->floor());
    stain->center_on(under);
    stain->stats()->set_old_family(FAMILY_SOLDIER);
    stain->stats()->set_level(3);
    return stain;
}

int act_until_snagged(walker* blade, walker* foe, int limit = 60)
{
    for (int tick = 1; tick <= limit; ++tick) {
        blade->act();
        if (frozen(foe) > 0)
            return tick;
        if (blade->dead())
            return -1;
    }
    return -1;
}

// The off twin of every captain row: with New Specials off, slots 3 and 4
// (and their shifts) are not in play, so no blade, banner or grunt appears.
void expect_captain_off_twin()
{
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 0;
    walker* captain = add_captain(tw, 200, 200);
    ASSERT_NE(nullptr, captain);
    add_living(tw, FAMILY_SOLDIER, kThem, 256, 200);
    add_stain(tw, captain);
    for (int slot : {3, 4}) {
        for (bool shifted : {false, true}) {
            const Cast c = cast(captain, slot, shifted);
            EXPECT_FALSE(c.ok) << "off: slot " << slot << " shifted " << shifted;
            EXPECT_EQ(walker::SpecialFailure::Unavailable, c.why);
        }
    }
    EXPECT_TRUE(blades(tw).empty()) << "off: no hook blade";
    EXPECT_TRUE(banners(tw).empty()) << "off: no banner";
    EXPECT_TRUE(grunts_of(tw, captain).empty()) << "off: no warband";
}

// A captain at (200,200) on an open field with New Specials on.
struct CaptainScene {
    TestGameWorld tw;
    walker* captain = nullptr;

    CaptainScene()
    {
        og::test::mount_core_pack();
        open_field(tw);
        tw.world().new_specials = 1;
        captain = add_captain(tw, 200, 200);
    }

    walker* throw_blade()
    {
        if (!cast(captain, 3, false).ok)
            return nullptr;
        const auto b = blades(tw);
        return b.empty() ? nullptr : b.front();
    }

    // Where the blade sits on its tick-th act (its top-left).
    std::pair<short, short> spot_at(walker* blade, int tick) const
    {
        const short base_x = static_cast<short>(
            captain->xpos() + captain->sizex() / 2 - blade->sizex() / 2);
        const short base_y = static_cast<short>(
            captain->ypos() + captain->sizey() / 2 - blade->sizey() / 2);
        const auto off = hook_offset_at(tick);
        return {static_cast<short>(base_x + std::lround(off.first)),
                static_cast<short>(base_y + std::lround(off.second))};
    }
};

}  // namespace

// effect_hook_blade.lua:50,51. The blade snags living foes on its own floor
// only. An enemy tent where the reeled soldier of the reel test is touched
// (tick 28), and an enemy soldier there on another floor, are each passed by
// for the whole 48-tick spiral: never the blade's leader, never on the hook,
// never cut or frozen. Off, there is no blade.
// Breaks: `local living = w:order() == C.ORDER_LIVING` -> `local living =
// true` (generator arm red); `local same_floor = w:floor() == self:floor()`
// -> `local same_floor = true` (other-floor arm red).
TEST(SemHookBlade, the_blade_snags_living_foes_on_its_floor_never_a_generator_or_another_floor)
{
    og::test::ScopedHookFailureGuard guard;
    {
        CaptainScene s;
        ASSERT_NE(nullptr, s.captain);
        walker* tent = add_generator(s.tw, kThem, 256, 200);
        ASSERT_NE(nullptr, tent);
        walker* blade = s.throw_blade();
        ASSERT_NE(nullptr, blade);
        for (int tick = 1; tick <= kHookTicks; ++tick) {
            blade->act();
            ASSERT_EQ(0, blade->dead()) << "tick " << tick;
            EXPECT_NE(tent, blade->leader()) << "generator: skipped, tick " << tick;
            EXPECT_EQ(0, blade->lineofsight()) << "generator: never on the hook, tick " << tick;
        }
        EXPECT_FLOAT_EQ(200.0f, tent->stats()->hitpoints()) << "generator: not cut";
        EXPECT_EQ(256, tent->xpos()) << "generator: not reeled";
        EXPECT_EQ(0, count_sound(s.tw, SOUND_CLANG));
    }
    {
        CaptainScene s;
        ASSERT_NE(nullptr, s.captain);
        s.tw.world().set_floor_count(2);
        walker* upstairs = add_living(s.tw, FAMILY_SOLDIER, kThem, 256, 200);
        ASSERT_NE(nullptr, upstairs);
        upstairs->set_floor(1);
        walker* blade = s.throw_blade();
        ASSERT_NE(nullptr, blade);
        for (int tick = 1; tick <= kHookTicks; ++tick) {
            blade->act();
            ASSERT_EQ(0, blade->dead()) << "tick " << tick;
            EXPECT_EQ(0, blade->lineofsight()) << "other floor: never on the hook, tick " << tick;
            EXPECT_EQ(0, frozen(upstairs)) << "other floor: not frozen, tick " << tick;
        }
        EXPECT_EQ(256, upstairs->xpos()) << "other floor: not reeled";
        EXPECT_FLOAT_EQ(200.0f, upstairs->stats()->hitpoints()) << "other floor: not cut";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_captain_off_twin();
}

// effect_hook_blade.lua:92-99. On the reel's last step the foe is set down on
// the ring spot beside the captain (218, one step from 224) and the blade is
// centred on it as it goes: on the act it dies its centre is the foe's.
// Break: line 99 `self:center_on(foe)` deleted (red: the blade's centre
// stays on the foe's last reel spot, 6 px east).
TEST(SemHookBlade, the_last_reel_step_sets_the_foe_down_beside_the_captain_with_the_blade_on_it)
{
    og::test::ScopedHookFailureGuard guard;
    CaptainScene s;
    ASSERT_NE(nullptr, s.captain);
    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kThem, 256, 200);
    ASSERT_NE(nullptr, foe);
    walker* blade = s.throw_blade();
    ASSERT_NE(nullptr, blade);
    ASSERT_EQ(28, act_until_snagged(blade, foe));
    int acts = 0;
    while (!blade->dead() && acts < 20) {
        blade->act();
        ++acts;
    }
    ASSERT_EQ(5, acts) << "248, 240, 232, 224, then the ring spot";
    EXPECT_EQ(218, foe->xpos()) << "ring spot within one step: set down there";
    EXPECT_EQ(200, foe->ypos());
    EXPECT_EQ(foe->xpos() + foe->sizex() / 2, blade->xpos() + blade->sizex() / 2)
        << "ring spot within one step: the blade centred on the foe";
    EXPECT_EQ(foe->ypos() + foe->sizey() / 2, blade->ypos() + blade->sizey() / 2);
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_captain_off_twin();
}

// effect_hook_blade.lua:135. A blade with a foe on the hook only reels: an
// enemy knife on the blade, and a second enemy soldier and knife where the
// blade would be next were it spinning (the soldier listed before the hooked
// one) are left alone: both knives fly on, the blade keeps its 30 hp, its
// leader is still the first foe, the second is neither frozen nor moved. (A
// second foe cannot touch the 6 px blade riding a 16 px foe without standing
// in that foe's next reel step, which ends the reel instead.)
// Break: `if self:lineofsight() > 0 then` -> `if self:lineofsight() > 1 then`
// (red: the blade spins on, eats the knife and snags again).
TEST(SemHookBlade, a_blade_with_a_foe_on_the_hook_neither_guards_nor_snags_another)
{
    og::test::ScopedHookFailureGuard guard;
    CaptainScene s;
    ASSERT_NE(nullptr, s.captain);
    walker* second = add_living(s.tw, FAMILY_SOLDIER, kThem, 450, 450);
    walker* first = add_living(s.tw, FAMILY_SOLDIER, kThem, 256, 200);
    ASSERT_NE(nullptr, first);
    ASSERT_NE(nullptr, second);
    walker* blade = s.throw_blade();
    ASSERT_NE(nullptr, blade);
    ASSERT_EQ(28, act_until_snagged(blade, first));
    ASSERT_EQ(1, blade->lineofsight());

    // A shot right on the blade, and a second foe and a second shot where
    // the blade would be on tick 29 were it still spinning (the second foe
    // clear of the hooked one's next reel step, so the reel goes on).
    walker* on_blade = s.tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, on_blade);
    on_blade->set_team_num(kThem);
    on_blade->setxy(blade->xpos(), blade->ypos());
    on_blade->set_damage(5.0f);
    const auto spot = s.spot_at(blade, 29);
    second->setxy(static_cast<short>(spot.first - 1), static_cast<short>(spot.second - 6));
    walker* knife = s.tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->set_team_num(kThem);
    knife->setxy(spot.first, spot.second);
    knife->set_damage(5.0f);
    const short second_x = second->xpos();
    const short second_y = second->ypos();

    blade->act();
    ASSERT_EQ(0, blade->dead());
    EXPECT_EQ(248, first->xpos()) << "on the hook: reel()";
    EXPECT_EQ(0, on_blade->dead()) << "on the hook: no projectile eating";
    EXPECT_EQ(0, knife->dead()) << "on the hook: no projectile eating";
    EXPECT_FLOAT_EQ(static_cast<float>(kHookHp), blade->stats()->hitpoints());
    EXPECT_EQ(first, blade->leader()) << "on the hook: no new snag";
    EXPECT_EQ(0, frozen(second)) << "on the hook: the second foe is not hooked";
    EXPECT_EQ(second_x, second->xpos());
    EXPECT_EQ(second_y, second->ypos());
    EXPECT_FLOAT_EQ(200.0f, second->stats()->hitpoints());
    EXPECT_EQ(1, count_sound(s.tw, SOUND_CLANG)) << "one snag only";
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_captain_off_twin();
}

// effect_hook_blade.lua:160. A blade whose guard spends its last hitpoint on
// a shot breaks that tick and snags nothing, even with a foe standing right
// on it: a damage-30 knife and an enemy soldier on the blade's tick-2 spot.
// Break: `if self.hp <= 0 then` -> `if self.hp < 0 then` (red: the soldier is
// snagged by the broken blade).
TEST(SemHookBlade, a_blade_broken_by_a_shot_snags_nothing_that_tick)
{
    og::test::ScopedHookFailureGuard guard;
    CaptainScene s;
    ASSERT_NE(nullptr, s.captain);
    walker* blade = s.throw_blade();
    ASSERT_NE(nullptr, blade);
    blade->act();  // tick 1
    const auto spot = s.spot_at(blade, 2);
    walker* knife = s.tw.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    ASSERT_NE(nullptr, knife);
    knife->set_team_num(kThem);
    knife->setxy(spot.first, spot.second);
    knife->set_damage(static_cast<float>(kHookHp));
    walker* soldier = add_living(s.tw, FAMILY_SOLDIER, kThem, spot.first, spot.second);
    ASSERT_NE(nullptr, soldier);

    blade->act();  // tick 2
    EXPECT_NE(0, knife->dead()) << "the shot is cut down";
    EXPECT_NE(0, blade->dead()) << "hp <= 0 after the guard: blade dead";
    EXPECT_EQ(0, frozen(soldier)) << "hp <= 0: no snag";
    EXPECT_NE(s.captain, soldier->foe());
    EXPECT_EQ(spot.first, soldier->xpos());
    EXPECT_EQ(spot.second, soldier->ypos());
    EXPECT_FLOAT_EQ(200.0f, soldier->stats()->hitpoints()) << "hp <= 0: no cut";
    EXPECT_EQ(0, count_sound(s.tw, SOUND_CLANG));
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_captain_off_twin();
}

// kit_captain.lua:58-63 (M.ring_spot) is asked through a probe below, with
// the gates' rows; its tie arm is there.

// kit_captain.lua:81-85. The captain's banner is his own LIVE war banner: a
// fallen banner of his, or another captain's standing one, is no banner, so
// his warband runs to him, not to it.
// Breaks: `and w:dead() == 0` -> `and true` (dead-banner arm red: the grunts
// run to the fallen banner's centre); `and w:owner() == self` -> `and true`
// (another captain's arm red).
TEST(SemCaptain, the_captains_banner_is_his_own_standing_one)
{
    og::test::ScopedHookFailureGuard guard;
    {
        CaptainScene s;
        ASSERT_NE(nullptr, s.captain);
        add_stain(s.tw, s.captain);
        ASSERT_TRUE(cast(s.captain, 4, false).ok);
        ASSERT_EQ(1u, banners(s.tw).size());
        walker* banner = banners(s.tw).front();
        banner->set_dead(1);
        s.captain->setxy(40, 200);
        ASSERT_TRUE(cast(s.captain, 4, true).ok);
        const auto grunts = grunts_of(s.tw, s.captain);
        ASSERT_EQ(2u, grunts.size());
        for (walker* g : grunts) {
            const command* go = front_command(g);
            ASSERT_NE(nullptr, go);
            EXPECT_EQ(COMMAND_GOTO, go->commandtype);
            EXPECT_EQ(40, go->com1) << "dead banner of his: nil, to the captain";
            EXPECT_EQ(200, go->com2);
        }
    }
    {
        CaptainScene s;
        ASSERT_NE(nullptr, s.captain);
        walker* other = add_captain(s.tw, 300, 300);
        ASSERT_NE(nullptr, other);
        add_stain(s.tw, other);
        ASSERT_TRUE(cast(other, 4, false).ok);
        ASSERT_EQ(1u, banners(s.tw).size());
        ASSERT_EQ(other, banners(s.tw).front()->owner());
        s.captain->setxy(40, 200);
        ASSERT_TRUE(cast(s.captain, 4, true).ok);
        const auto grunts = grunts_of(s.tw, s.captain);
        ASSERT_EQ(2u, grunts.size());
        for (walker* g : grunts) {
            const command* go = front_command(g);
            ASSERT_NE(nullptr, go);
            EXPECT_EQ(40, go->com1) << "another captain's live banner: nil";
            EXPECT_EQ(200, go->com2);
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_captain_off_twin();
}

// kit_captain.lua:96-101. The warband is the live orcs this captain summoned:
// four wild orcs on his team do not count toward the cap of 6 (a level-19
// captain still calls 5), and five grunts of his that fell do not either (a
// second call brings 5 more).
// Breaks: `and ob:owner() == self` -> `and true` (wild-orc arm red: 2
// grunts); `and ob:dead() == 0` -> `and true` (dead-grunt arm red: 1 grunt).
TEST(SemCaptain, the_warband_counts_only_his_own_live_grunts)
{
    og::test::ScopedHookFailureGuard guard;
    {
        CaptainScene s;
        ASSERT_NE(nullptr, s.captain);
        s.captain->setxy(40, 200);
        s.captain->stats()->set_level(19);
        for (short x : {short{300}, short{320}, short{340}, short{360}})
            ASSERT_NE(nullptr, add_living(s.tw, FAMILY_ORC, kUs, x, 400));
        ASSERT_TRUE(cast(s.captain, 4, true).ok);
        EXPECT_EQ((std::vector<short>{0, 16, 64, 80, 96}),
                  xs_of(grunts_of(s.tw, s.captain)))
            << "a wild orc (not his): not counted toward the cap";
    }
    {
        CaptainScene s;
        ASSERT_NE(nullptr, s.captain);
        s.captain->setxy(40, 200);
        s.captain->stats()->set_level(19);
        ASSERT_TRUE(cast(s.captain, 4, true).ok);
        auto fallen = grunts_of(s.tw, s.captain);
        ASSERT_EQ(5u, fallen.size());
        for (walker* g : fallen)
            g->set_dead(1);
        const Cast again = cast(s.captain, 4, true);
        ASSERT_TRUE(again.ok) << again.reason;
        EXPECT_EQ(5u, grunts_of(s.tw, s.captain).size())
            << "a dead grunt: not counted";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_captain_off_twin();
}

// ===========================================================================
// effect_mine.lua — the thief's MINE (New Specials; off, the shifted cast is
// the classic bomb and no mine exists)
// ===========================================================================

namespace {

constexpr unsigned char kThiefTeam = 0;

// A seated level-`level` thief at (160,160), New Specials at `ns`.
struct MineScene {
    TestGameWorld tw;
    walker* thief = nullptr;

    explicit MineScene(short level, short ns = 1)
    {
        og::test::mount_core_pack();
        open_field(tw);
        tw.world().new_specials = ns;
        thief = add_living(tw, FAMILY_THIEF, kThiefTeam, 160, 160, level);
        if (thief == nullptr)
            return;
        thief->set_user(0);
        thief->set_act_type(ACT_CONTROL);
        thief->stats()->set_max_magicpoints(500.0f);
        thief->stats()->set_magicpoints(500.0f);
    }

    walker* lay_mine()
    {
        const auto before = live_of(tw.world().oblist, Order::FX, fx_family("core:mine"));
        if (!cast(thief, 1, true).ok)
            return nullptr;
        const auto after = live_of(tw.world().oblist, Order::FX, fx_family("core:mine"));
        if (after.size() != before.size() + 1)
            return nullptr;
        return after.back();
    }

    int explosions() const
    {
        return static_cast<int>(live_of(tw.world().oblist, Order::FX, FAMILY_EXPLOSION).size());
    }
    int clouds() const
    {
        return static_cast<int>(live_of(tw.world().oblist, Order::FX, FAMILY_CLOUD).size());
    }
};

void expect_mine_off_twin(short level)
{
    MineScene off(level, 0);
    ASSERT_NE(nullptr, off.thief);
    EXPECT_TRUE(cast(off.thief, 1, true).ok) << "off: the shifted cast is DROP BOMB";
    EXPECT_TRUE(live_of(off.tw.world().oblist, Order::FX, fx_family("core:mine")).empty())
        << "off: no mine";
    EXPECT_EQ(1u, live_of(off.tw.world().oblist, Order::FX, FAMILY_BOMB).size())
        << "off: the classic bomb";
}

}  // namespace

// effect_mine.lua:45. A mine whose lifetime runs out expires with its hp set
// to -1 (the mark on_death reads), and so with no blast.
// Break: `self.hp = -1` -> `self.hp = 0` (red: hp 0 and a blast).
TEST(SemMine, an_expired_mine_goes_with_hp_minus_one_and_no_blast)
{
    og::test::ScopedHookFailureGuard guard;
    MineScene s(1);
    ASSERT_NE(nullptr, s.thief);
    walker* mine = s.lay_mine();
    ASSERT_NE(nullptr, mine);
    s.thief->setxy(40, 40);
    mine->set_lifetime(2);
    ASSERT_TRUE(mine->act());
    EXPECT_EQ(0, mine->dead()) << "time left: still armed";
    EXPECT_EQ(1, mine->lifetime());
    ASSERT_TRUE(mine->act());
    EXPECT_NE(0, mine->dead()) << "lifetime reaches 0: dead";
    EXPECT_FLOAT_EQ(-1.0f, mine->stats()->hitpoints()) << "lifetime reaches 0: hp -1";
    EXPECT_EQ(0, s.explosions()) << "lifetime reaches 0: no explosion";
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_mine_off_twin(1);
}

// effect_mine.lua:66. A level-7 mine's poison puff is skipped when the world
// has no room for it (the cloud's factory refuses); the blast still happens
// and nothing errors.
// Break: `if not puff then` -> `if false then` (red: a script error on the
// nil puff).
TEST(SemMine, a_full_world_skips_the_mines_puff_but_not_its_blast)
{
    og::test::ScopedHookFailureGuard guard;
    MineScene s(7);
    ASSERT_NE(nullptr, s.thief);
    walker* mine = s.lay_mine();
    ASSERT_NE(nullptr, mine);
    s.thief->setxy(40, 40);
    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kThem, mine->xpos(), mine->ypos());
    ASSERT_NE(nullptr, foe);
    const auto factory = s.tw.world().entity_factory;
    s.tw.world().entity_factory = [factory](Order order, std::int32_t family) {
        if (order == Order::FX && family == FAMILY_CLOUD)
            return std::unique_ptr<walker>();
        return factory(order, family);
    };
    ASSERT_TRUE(mine->act());
    s.tw.world().entity_factory = factory;
    EXPECT_NE(0, mine->dead()) << "it blew";
    EXPECT_EQ(1, s.explosions()) << "world full: the explosion still spawned";
    EXPECT_EQ(0, s.clouds()) << "world full: no FAMILY_CLOUD";
    EXPECT_EQ(0u, guard.count()) << "world full: the puff is skipped quietly: "
                                 << guard.message();
    expect_mine_off_twin(7);
}

// effect_mine.lua:87-92. A blast lights only other LIVE MINES of its team
// within 24 px: a live neighbour 16 px off is lit, while a fallen mine of the
// same team and a same-team effect that is not a mine (a magic shield), both
// as close, are left as they were.
// Breaks: `and ob:family() == FX_MINE` -> `and true` (not-a-mine arm red);
// `and ob:dead() == 0` -> `and true` (dead-mine arm red).
TEST(SemMine, a_blast_lights_only_live_mines_of_its_team_nearby)
{
    og::test::ScopedHookFailureGuard guard;
    MineScene s(2);
    ASSERT_NE(nullptr, s.thief);
    walker* first = s.lay_mine();
    ASSERT_NE(nullptr, first);
    s.thief->setxy(176, 160);
    walker* near = s.lay_mine();
    ASSERT_NE(nullptr, near);
    s.thief->setxy(160, 176);
    walker* fallen = s.lay_mine();
    ASSERT_NE(nullptr, fallen);
    s.thief->setxy(40, 40);
    ASSERT_LE(first->distance_to_ob(near), 24);
    ASSERT_LE(first->distance_to_ob(fallen), 24);
    fallen->set_dead(1);
    walker* shield = s.tw.world().add_fx_ob(Order::FX, FAMILY_MAGIC_SHIELD);
    ASSERT_NE(nullptr, shield);
    shield->set_team_num(kThiefTeam);
    shield->setxy(static_cast<short>(first->xpos() + 8), first->ypos());
    shield->set_ani_type(static_cast<char>(ANI_WALK));
    ASSERT_LE(first->distance_to_ob(shield), 24);

    walker* foe = add_living(s.tw, FAMILY_SOLDIER, kThem, first->xpos(), first->ypos());
    ASSERT_NE(nullptr, foe);
    ASSERT_TRUE(first->act());
    ASSERT_NE(0, first->dead());
    EXPECT_EQ(ANI_EXPLODE, static_cast<int>(near->ani_type()))
        << "same-team live mine within 24 px: lit";
    EXPECT_EQ(ANI_WALK, static_cast<int>(fallen->ani_type())) << "dead mine: unchanged";
    EXPECT_EQ(ANI_WALK, static_cast<int>(shield->ani_type())) << "not a mine: unchanged";
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_mine_off_twin(2);
}

// effect_mine.lua:101. A mine that ran out of time leaves without its blast,
// and so lights nothing: its neighbour 16 px off stays dark and, with no foe
// on it, stays armed on its own next act.
// Break: `if self.hp < 0 then` -> `if self.hp < -5 then` (red: a blast, and
// the neighbour lit and blown).
TEST(SemMine, an_expired_mine_lights_no_neighbour)
{
    og::test::ScopedHookFailureGuard guard;
    MineScene s(9);
    ASSERT_NE(nullptr, s.thief);
    walker* old_mine = s.lay_mine();
    ASSERT_NE(nullptr, old_mine);
    s.thief->setxy(176, 160);
    walker* neighbour = s.lay_mine();
    ASSERT_NE(nullptr, neighbour);
    s.thief->setxy(40, 40);
    ASSERT_LE(old_mine->distance_to_ob(neighbour), 24);
    old_mine->set_lifetime(1);
    ASSERT_TRUE(old_mine->act());
    ASSERT_NE(0, old_mine->dead());
    EXPECT_EQ(0, s.explosions()) << "hp < 0: no explosion";
    EXPECT_EQ(0, s.clouds()) << "hp < 0: no puff";
    EXPECT_EQ(ANI_WALK, static_cast<int>(neighbour->ani_type()))
        << "hp < 0: neighbours not lit";
    ASSERT_TRUE(neighbour->act());
    EXPECT_EQ(0, neighbour->dead()) << "the neighbour stays armed";
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_mine_off_twin(9);
}

// ===========================================================================
// effect_wail.lua — the ghost's WAIL (New Specials; off, Shift + SCARE is the
// classic scare and no wail bolt exists)
// ===========================================================================

namespace {

int wail_family()
{
    return fx_family("core:wail");
}

walker* add_ghost(TestGameWorld& tw, unsigned char team, short x, short y)
{
    walker* g = add_living(tw, FAMILY_GHOST, team, x, y, 10);
    if (g == nullptr)
        return nullptr;
    g->stats()->set_max_magicpoints(100.0f);
    g->stats()->set_magicpoints(100.0f);
    return g;
}

walker* add_orc(TestGameWorld& tw, short x, short y)
{
    walker* orc = add_living(tw, FAMILY_ORC, kThem, x, y);
    if (orc != nullptr)
        orc->set_act_type(ACT_GUARD);
    return orc;
}

// A bolt made the way the cast makes one, homing on `foe` with `hops` hops.
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

// Puts the 48x40 puff's centre on `foe`'s centre (it strikes on its next act).
void centre_on_foe(walker* bolt, const walker* foe)
{
    bolt->setxy(static_cast<short>(foe->xpos() + foe->sizex() / 2 - bolt->sizex() / 2),
                static_cast<short>(foe->ypos() + foe->sizey() / 2 - bolt->sizey() / 2));
}

int wails_made(TestGameWorld& tw)
{
    return static_cast<int>(all_of(tw.world().oblist, Order::FX, wail_family()).size());
}

void expect_wail_off_twin()
{
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 0;
    walker* ghost = add_ghost(tw, 0, 100, 100);
    ASSERT_NE(nullptr, ghost);
    add_orc(tw, 140, 100);
    EXPECT_TRUE(cast(ghost, 1, true).ok) << "off: Shift + SCARE is the scare";
    EXPECT_EQ(0, wails_made(tw)) << "off: no wail bolt";
    EXPECT_EQ(1u, all_of(tw.world().oblist, Order::FX, FAMILY_GHOST_SCARE).size())
        << "off: the classic scare cloud";
}

struct WailScene {
    TestGameWorld tw;
    walker* ghost = nullptr;

    explicit WailScene(short gx = 100, short gy = 100)
    {
        og::test::mount_core_pack();
        open_field(tw);
        tw.world().new_specials = 1;
        ghost = add_ghost(tw, 0, gx, gy);
    }
};

}  // namespace

// effect_wail.lua:20-33. The cast's bolt carries the ghost's team and level,
// does no damage, and flies on the floor it leaves from: a team-2 ghost on
// floor 1 at level 10 makes a team-2, level-10, damage-0, floor-1 bolt.
// Breaks: `bolt.team = owner.team` deleted; `bolt.level = owner.level`
// deleted; `bolt.damage = 0` -> `bolt.damage = 1`; `bolt:set_floor(
// from:floor())` -> `bolt:set_floor(0)` (each red on its own line).
TEST(SemWail, a_wail_bolt_carries_its_ghosts_team_level_and_floor_and_no_damage)
{
    og::test::ScopedHookFailureGuard guard;
    og::test::mount_core_pack();
    TestGameWorld tw;
    open_field(tw);
    tw.world().new_specials = 1;
    tw.world().set_floor_count(2);
    walker* ghost = add_ghost(tw, 2, 100, 100);
    walker* orc = add_orc(tw, 140, 100);
    ASSERT_NE(nullptr, ghost);
    ASSERT_NE(nullptr, orc);
    ghost->set_floor(1);
    orc->set_floor(1);
    const Cast c = cast(ghost, 1, true);
    ASSERT_TRUE(c.ok) << c.reason;
    const auto bolts = live_of(tw.world().oblist, Order::FX, wail_family());
    ASSERT_EQ(1u, bolts.size()) << "spawned";
    walker* bolt = bolts.front();
    EXPECT_EQ(2, bolt->team_num()) << "spawned: the ghost's team";
    EXPECT_EQ(10, bolt->stats()->level()) << "spawned: the ghost's level";
    EXPECT_FLOAT_EQ(0.0f, bolt->damage()) << "spawned: damage 0";
    EXPECT_EQ(1, bolt->floor()) << "spawned: the ghost's floor";
    EXPECT_EQ(ghost, bolt->owner());
    EXPECT_EQ(orc, bolt->leader());
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_wail_off_twin();
}

// effect_wail.lua:69,73. A wailed monster flees away from the GHOST (sign of
// the offset) for a quarter of the level-10 scare, 62 ticks, and the strike
// draws nothing for it; a hero takes a rand0(constitution) roll off the 62
// (one draw); a hero whose roll is 62 or more does not flee, and is still
// marked for 3 rounds.
// Breaks: `if leader:has_guy() then` -> `if false then` (hero arm red);
// `if fright > 0 then` -> `if true then` (full-resist arm red); `og.sign(
// leader:xpos() - owner:xpos())` -> `og.sign(owner:xpos() - leader:xpos())`
// (direction red); an `og.rand0(7)` added above the has_guy test (monster
// no-draw red).
TEST(SemWail, a_wailed_foe_flees_from_the_ghost_less_a_heros_constitution_roll)
{
    og::test::ScopedHookFailureGuard guard;
    {
        WailScene s;
        walker* orc = add_orc(s.tw, 140, 80);
        ASSERT_NE(nullptr, orc);
        walker* bolt = hand_bolt(s.tw, s.ghost, orc, 0);
        ASSERT_NE(nullptr, bolt);
        centre_on_foe(bolt, orc);
        s.tw.world().rng_.state_ = 9001u;
        bolt->act();
        ASSERT_NE(0, bolt->dead()) << "struck on its first act";
        const command* flee = front_command(orc);
        ASSERT_NE(nullptr, flee) << "monster: frightened";
        EXPECT_EQ(COMMAND_WALK, flee->commandtype);
        EXPECT_TRUE(flee->forced);
        EXPECT_EQ(62, flee->commandcount) << "monster: scare_duration(10) / 4";
        EXPECT_EQ(1, flee->com1) << "monster: east, away from the ghost";
        EXPECT_EQ(-1, flee->com2) << "monster: north, away from the ghost";
        EXPECT_EQ(9001u, s.tw.world().rng_.state_) << "monster: no draw";
        EXPECT_EQ(3, orc->skip_exit());
    }
    {
        WailScene s;
        walker* hero = add_orc(s.tw, 140, 80);
        ASSERT_NE(nullptr, hero);
        make_hero(hero, 30);
        walker* bolt = hand_bolt(s.tw, s.ghost, hero, 0);
        ASSERT_NE(nullptr, bolt);
        centre_on_foe(bolt, hero);
        const std::uint32_t pin =
            state_where([](og::sim::SimRandom& r) { return r.next(30) == 9; });
        ASSERT_NE(0u, pin);
        s.tw.world().rng_.state_ = pin;
        bolt->act();
        ASSERT_NE(0, bolt->dead());
        const command* flee = front_command(hero);
        ASSERT_NE(nullptr, flee) << "hero: frightened";
        EXPECT_EQ(62 - 9, flee->commandcount) << "hero: rand0(con) subtracted";
        EXPECT_EQ(state_after(pin, {30}), s.tw.world().rng_.state_)
            << "hero: one rand0(constitution) draw";
        EXPECT_EQ(3, hero->skip_exit());
    }
    {
        WailScene s;
        walker* hero = add_orc(s.tw, 140, 80);
        ASSERT_NE(nullptr, hero);
        make_hero(hero, 200);
        walker* bolt = hand_bolt(s.tw, s.ghost, hero, 0);
        ASSERT_NE(nullptr, bolt);
        centre_on_foe(bolt, hero);
        const std::uint32_t pin =
            state_where([](og::sim::SimRandom& r) { return r.next(200) >= 62; });
        ASSERT_NE(0u, pin);
        s.tw.world().rng_.state_ = pin;
        bolt->act();
        ASSERT_NE(0, bolt->dead());
        EXPECT_FALSE(hero->stats()->has_commands()) << "fright <= 0: no command";
        EXPECT_EQ(3, hero->skip_exit()) << "fright <= 0: skip_exit still +3";
        EXPECT_EQ(state_after(pin, {200}), s.tw.world().rng_.state_);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_wail_off_twin();
}

// effect_wail.lua:82. A bolt with no hops left frightens and stops, drawing
// no fork budget; a bolt with a hop left draws its budget, rand0(10) + 1,
// even when no other foe stands in reach (and then forks nowhere).
// Breaks: `if hops < 1 then` -> `if hops < 0 then` (hops-0 arm red: a draw);
// -> `if hops < 2 then` (hops-1 arm red: no draw).
TEST(SemWail, a_bolt_with_no_hops_left_forks_no_further_and_draws_no_budget)
{
    og::test::ScopedHookFailureGuard guard;
    for (const int hops : {0, 1}) {
        WailScene s;
        walker* orc = add_orc(s.tw, 300, 300);
        ASSERT_NE(nullptr, orc);
        walker* bolt = hand_bolt(s.tw, s.ghost, orc, hops);
        ASSERT_NE(nullptr, bolt);
        centre_on_foe(bolt, orc);
        s.tw.world().rng_.state_ = 777u;
        bolt->act();
        ASSERT_NE(0, bolt->dead()) << "hops " << hops;
        EXPECT_EQ(62, front_command(orc) ? front_command(orc)->commandcount : -1)
            << "hops " << hops << ": the foe is frightened either way";
        EXPECT_EQ(1, wails_made(s.tw)) << "hops " << hops << ": no fork bolt";
        if (hops == 0) {
            EXPECT_EQ(777u, s.tw.world().rng_.state_) << "hops 0: no rand0 draw";
        } else {
            EXPECT_EQ(state_after(777u, {10}), s.tw.world().rng_.state_)
                << "hops >= 1: one rand0(level) fork-budget draw";
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_wail_off_twin();
}

// effect_wail.lua:95,98. The fork budget (rand0(10) + 1, pinned here to 1) is
// spent only on eligible foes, in list order: an orc already wailed at
// (skip_exit 3) listed first is skipped without using it, the next orc gets
// the one fork, and the orc after that gets none.
// Breaks: `forks_left = forks_left - 1` moved above the is_fork_target test
// (ineligible arm red: no fork at all); `if forks_left <= 0 then` -> `if
// forks_left < 0 then` (budget-spent arm red: a second fork).
TEST(SemWail, a_wail_forks_to_at_most_its_budget_and_skipped_foes_keep_it)
{
    og::test::ScopedHookFailureGuard guard;
    WailScene s(60, 60);
    walker* struck = add_orc(s.tw, 300, 300);
    walker* skipped = add_orc(s.tw, 340, 300);
    walker* first = add_orc(s.tw, 360, 300);
    walker* second = add_orc(s.tw, 380, 300);
    ASSERT_NE(nullptr, second);
    skipped->set_skip_exit(3);
    walker* bolt = hand_bolt(s.tw, s.ghost, struck, 1);
    ASSERT_NE(nullptr, bolt);
    centre_on_foe(bolt, struck);
    const std::uint32_t pin =
        state_where([](og::sim::SimRandom& r) { return r.next(10) == 0; });
    ASSERT_NE(0u, pin);
    s.tw.world().rng_.state_ = pin;
    bolt->act();
    ASSERT_NE(0, bolt->dead());
    const auto forks = live_of(s.tw.world().oblist, Order::FX, wail_family());
    ASSERT_EQ(1u, forks.size()) << "budget 1: exactly one fork";
    EXPECT_EQ(first, forks.front()->leader())
        << "ineligible: skipped, budget kept for the next orc";
    EXPECT_EQ(0, forks.front()->lifetime()) << "the fork carries hops - 1";
    EXPECT_EQ(3, skipped->skip_exit()) << "the skipped orc was not struck";
    EXPECT_FALSE(skipped->stats()->has_commands());
    EXPECT_FALSE(second->stats()->has_commands()) << "budget spent: stop";
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_wail_off_twin();
}

// effect_wail.lua:115-122. The puff steers its centre onto its foe's centre at
// most 6 px per axis an act, never past it, and an axis already level does
// not move; it faces where it flies.
// Breaks: `xd = og.min(self:stepsize(), tx - self:xpos())` and the y twin ->
// `... = self:stepsize()` (right/below arm red); `og.max(ty - self:ypos(),
// -self:stepsize())` and the x twin -> `-self:stepsize()` (left/above arm
// red); `local xd, yd = 0, 0` -> `local xd, yd = 0, 1` (aligned arm red).
TEST(SemWail, a_wail_puff_homes_at_most_six_px_per_axis_and_never_overshoots)
{
    og::test::ScopedHookFailureGuard guard;
    // The orc at (300,300): the puff's centre is on its centre at (284,288).
    const struct {
        short bx, by;
        short dx, dy;
        const char* arm;
    } cases[] = {
        {264, 285, 6, 3, "target right/below: +6, and +3 (the gap) on y"},
        {281, 268, 3, 6, "target right/below: +3 (the gap) on x, +6 on y"},
        {304, 308, -6, -6, "target left/above: -6, -6"},
        {304, 290, -6, -2, "target left/above: -6, and -2 (the gap) on y"},
        {264, 288, 6, 0, "aligned y: y does not move"},
        {284, 268, 0, 6, "aligned x: x does not move"},
    };
    for (const auto& c : cases) {
        WailScene s;
        walker* orc = add_orc(s.tw, 300, 300);
        ASSERT_NE(nullptr, orc);
        walker* bolt = hand_bolt(s.tw, s.ghost, orc, 0);
        ASSERT_NE(nullptr, bolt);
        ASSERT_EQ(48, bolt->sizex());
        ASSERT_EQ(40, bolt->sizey());
        bolt->setxy(c.bx, c.by);
        bolt->act();
        ASSERT_EQ(0, bolt->dead()) << c.arm << ": not yet struck";
        EXPECT_FLOAT_EQ(static_cast<float>(c.bx + c.dx), bolt->worldx()) << c.arm;
        EXPECT_FLOAT_EQ(static_cast<float>(c.by + c.dy), bolt->worldy()) << c.arm;
        EXPECT_EQ(bolt->facing(c.dx, c.dy), static_cast<short>(bolt->curdir()))
            << c.arm << ": faces where it flies";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_wail_off_twin();
}

// effect_wail.lua:139,142. A bolt that flies out its 60-tick life fades
// without a fright: it is alive for 60 acts short of a far orc and gone on the
// 61st, the orc never frightened nor marked.
// Break: `if self:lineofsight() < 1` -> `if self:lineofsight() < 0` (red:
// alive on the 61st act).
TEST(SemWail, a_wail_bolt_that_flies_out_its_life_fades_without_a_fright)
{
    og::test::ScopedHookFailureGuard guard;
    WailScene s;
    walker* orc = add_orc(s.tw, 500, 100);
    ASSERT_NE(nullptr, orc);
    walker* bolt = hand_bolt(s.tw, s.ghost, orc, 0);
    ASSERT_NE(nullptr, bolt);
    for (int act = 1; act <= 60; ++act) {
        bolt->act();
        ASSERT_EQ(0, bolt->dead()) << "act " << act << ": still flying";
    }
    EXPECT_EQ(0, bolt->lineofsight());
    bolt->act();
    EXPECT_NE(0, bolt->dead()) << "lineofsight < 1: dead";
    EXPECT_FALSE(orc->stats()->has_commands()) << "lineofsight < 1: no fright";
    EXPECT_EQ(0, orc->skip_exit());
    EXPECT_EQ(0u, guard.count()) << guard.message();
    expect_wail_off_twin();
}

// ===========================================================================
// gates.lua and kit_captain.ring_spot, asked through a probe: the soldier's
// do_special replaced by a Lua body that writes each answer with og.log as
// "<key>\t<value>" (the BotGatesLib idiom, test_bot_gates_lib.cpp). gates.lua
// has no old-game counterpart; its flight_px mirrors walker.cpp's
// configure_weapon_profile_base, whose straight-heading scaling (FACE_UP,
// RIGHT, DOWN, LEFT: los * 309 / 256, step * 362 / 256) is the old game's
// walker.cpp fire() switch exactly.
// ===========================================================================

namespace {

using og::test::bot_gates::GameplayFixesBots;
using Answers = std::map<std::string, std::string>;

constexpr const char* kPrelude =
    "local G = og.use('core:gates')\n"
    "local K = og.use('core:kit_captain')\n"
    "local C = og.C\n"
    "local function where(w)\n"
    "  if w == nil then\n"
    "    return 'nil'\n"
    "  end\n"
    "  return w:xpos() .. ',' .. w:ypos()\n"
    "end\n";

class SemGatesProbe : public GameplayFixesBots {
protected:
    living* soldier(unsigned char team, short x, short y)
    {
        return body(add(FAMILY_SOLDIER, team, x, y, 1), 16, 16);
    }

    Answers probe(walker* self, const std::string& lua)
    {
        og::script::register_pack_script(
            {"test.sem_libs_2", "sem_libs_2_probe.lua",
             std::string(kPrelude) +
                 "og.register_hooks('living', 'core:soldier', {\n"
                 "  do_special = function(self)\n"
                 "    og.log('<probe>')\n" +
                 lua +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
        const std::uint32_t before = world().rng_.state_;
        const auto handled = og::script::hooks::do_special(
            get_family_descriptor(FAMILY_SOLDIER), self);
        EXPECT_TRUE(handled.has_value());
        EXPECT_EQ(before, world().rng_.state_) << "the probe draws nothing";
        Answers out;
        const auto& log = world().scripts().host().log();
        std::size_t start = log.size();
        while (start > 0 && log[start - 1] != "<probe>")
            --start;
        for (std::size_t i = start; i < log.size(); ++i) {
            const std::string& line = log[i];
            const auto tab = line.find('\t');
            if (tab == std::string::npos)
                out[line] = "";
            else
                out[line.substr(0, tab)] = line.substr(tab + 1);
        }
        return out;
    }
};

}  // namespace

// gates.lua:269-271. FACE_LEFT is a straight heading: a level-1 rock (step 5,
// los 8) flies 56 px to the left, as it does right and up, while the
// down-right diagonal gets the unscaled 35.
// Break: `or heading == C.FACE_LEFT` -> `or heading == -99` (red: 35 to the
// left).
TEST_F(SemGatesProbe, a_shot_flies_further_on_all_four_straight_headings_than_on_a_diagonal)
{
    living* self = soldier(1, 200, 200);
    ASSERT_NE(nullptr, self);
    const Answers a = probe(self,
        "    og.log('left', G.flight_px(self, 5, 8, C.FACE_LEFT))\n"
        "    og.log('right', G.flight_px(self, 5, 8, C.FACE_RIGHT))\n"
        "    og.log('down', G.flight_px(self, 5, 8, C.FACE_DOWN))\n"
        "    og.log('down_right', G.flight_px(self, 5, 8, C.FACE_DOWN_RIGHT))\n");
    EXPECT_EQ("56", a.at("left")) << "straight heading FACE_LEFT: scaled flight";
    EXPECT_EQ("56", a.at("right"));
    EXPECT_EQ("56", a.at("down"));
    EXPECT_EQ("35", a.at("down_right")) << "diagonal: unscaled flight";
}

// gates.lua:369-383. With none of a kind owned, owned() answers a count of 0
// AND no first one (nil): no FX circle, no weapon boomerang, no living tent.
// Break: `local first = nil` -> `local first = self` (red: the caster's own
// corner where nil belongs).
TEST_F(SemGatesProbe, owned_answers_zero_and_no_first_when_the_caster_owns_none)
{
    living* self = soldier(1, 200, 200);
    ASSERT_NE(nullptr, self);
    walker* circle = world().add_weap_ob(Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    ASSERT_NE(nullptr, circle);
    circle->setxy(200, 230);
    circle->set_owner(self);
    const std::string ring = std::to_string(FAMILY_CIRCLE_PROTECTION);
    const std::string boomerang = std::to_string(FAMILY_BOOMERANG);
    const Answers a = probe(self,
        "    local n, first = G.owned(self, C.ORDER_FX, " + ring + ")\n"
        "    og.log('fx_ring', n)\n"
        "    og.log('fx_ring_first', where(first))\n"
        "    n, first = G.owned(self, C.ORDER_WEAPON, " + boomerang + ")\n"
        "    og.log('weapon_boomerang', n)\n"
        "    og.log('weapon_boomerang_first', where(first))\n"
        "    n, first = G.owned(self, C.ORDER_WEAPON, " + ring + ")\n"
        "    og.log('weapon_ring', n)\n"
        "    og.log('weapon_ring_first', where(first))\n");
    EXPECT_EQ("0", a.at("fx_ring")) << "none: count 0 (the circle is a weapon)";
    EXPECT_EQ("nil", a.at("fx_ring_first")) << "none: first nil";
    EXPECT_EQ("0", a.at("weapon_boomerang")) << "none: count 0";
    EXPECT_EQ("nil", a.at("weapon_boomerang_first")) << "none: first nil";
    EXPECT_EQ("1", a.at("weapon_ring")) << "the control: the circle is his";
    EXPECT_EQ("200,230", a.at("weapon_ring_first"));
}

// kit_captain.lua:58-63. Of two clear ring spots equally near the point the
// foe is reeled from, the first clockwise from straight up wins: for a 16 px
// anchor at (200,200) the N spot (200,182) and the NE spot (218,182) are both
// 9 px from (209,182), and N is answered; with N taken, NE. (A pure helper:
// the New Specials setting gates the hook that calls it, not the geometry;
// the hook's off twin is in the SemHookBlade rows.)
// Break: `best_x == nil or d < best_d` -> `best_x == nil or d <= best_d`
// (red: 218,182 for the tie).
TEST_F(SemGatesProbe, a_reeled_foe_takes_the_first_clockwise_of_two_equally_near_ring_spots)
{
    walker* anchor = body(add(FAMILY_BIG_ORC, 0, 200, 200, 10), 16, 16);
    living* mover = soldier(1, 400, 400);
    ASSERT_NE(nullptr, anchor);
    ASSERT_NE(nullptr, mover);
    const std::string ask =
        "    local anchor = nil\n"
        "    local obs = og.oblist()\n"
        "    for i = 1, #obs do\n"
        "      if obs[i]:xpos() == 200 and obs[i]:ypos() == 200 then anchor = obs[i] end\n"
        "    end\n"
        "    local x, y = K.ring_spot(self, anchor, 209, 182)\n"
        "    og.log('spot', tostring(x) .. ',' .. tostring(y))\n";
    Answers a = probe(mover, ask);
    EXPECT_EQ("200,182", a.at("spot")) << "tie: first clockwise from up";
    ASSERT_NE(nullptr, soldier(0, 200, 182));
    a = probe(mover, ask);
    EXPECT_EQ("218,182", a.at("spot")) << "N taken: the NE spot";
}
