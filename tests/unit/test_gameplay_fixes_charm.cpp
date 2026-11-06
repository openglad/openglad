/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Gameplay Fixes AD (#412): a charmed walker fights for the colour it wears.
//
// CHARM OPPONENT (the thief) and MIND CONTROL (the archmage) change a
// walker's team and start its charm clock, and never touch its owner. Every
// alliance question climbs the owner chain to its head (walker::is_friendly,
// is_friendly_to_team, the climb in walker::attack, the kill credit), so in
// the classic game a charmed tent troop, or a charmed summon of an enemy
// caster, still answers for its old side: the side that charmed it sees a
// foe and hits it, it fights its charmer and refuses its old friends. With
// the setting on every climb stops at a walker whose charm clock is running
// (walker::charmed_head, charm_left() > 0), so for the charm's length the
// troop is its own head; its owner, its lifetime and the tent tie are not
// touched, and when the charm runs out the chain is what it was.
//
// The scene: a red (team 0) thief or archmage and a red soldier, a green
// (team 1) tent with its real spawn and a green soldier. Red is the
// player's team (my_team 0), so a red attacker scores the classic team
// score.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace {

constexpr unsigned char kRed = 0;
constexpr unsigned char kGreen = 1;

struct CharmScene
{
    std::unique_ptr<TestGameWorld> tw;
    walker* tent = nullptr;
    walker* troop = nullptr;
    walker* red = nullptr;
    walker* green = nullptr;

    explicit CharmScene(short fixes)
    {
        og::test::mount_core_pack();
        tw = std::make_unique<TestGameWorld>();
        GameWorld& w = tw->world();
        w.resize_grid(32, 32);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = fixes;
        w.new_specials = 0;

        tent = w.add_ob(Order::Generator, FAMILY_TENT);
        if (tent == nullptr)
            return;
        tent->set_team_num(kGreen);
        tent->setxy(320, 360);
        tent->stats()->set_level(1);
        // The tent's own spawn: owner = the tent, lifetime 800 + 11.
        troop = tent->fire();
        if (troop == nullptr)
            return;
        troop->setxy(100, 100);
        troop->stats()->set_level(1);
        // ACT_SIT, not ACT_CONTROL: with the setting on a charm refuses a
        // body a seat drives (walker:seat_driven reads ACT_CONTROL).
        troop->set_act_type(ACT_SIT);
        red = add(FAMILY_SOLDIER, kRed, 100, 240);
        green = add(FAMILY_SOLDIER, kGreen, 240, 240);
    }

    GameWorld& world() { return tw->world(); }

    walker* add(int family, unsigned char team, int x, int y)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->setxy(static_cast<short>(x), static_cast<short>(y));
        w->set_act_type(ACT_CONTROL);
        return w;
    }

    // A red caster 30 px east of the troop with magic to spare.
    walker* add_caster(int family, short level, char slot, short shift)
    {
        walker* c = add(family, kRed, 130, 100);
        if (c == nullptr)
            return nullptr;
        c->stats()->set_level(level);
        c->stats()->set_max_magicpoints(500.0f);
        c->stats()->set_magicpoints(500.0f);
        c->set_busy(0);
        c->set_current_special(slot);
        c->set_shifter_down(shift);
        return c;
    }
};

// One blow of `damage` from a to b: did it land, and what did it take?
struct Blow
{
    bool landed = false;
    float hp_lost = 0.0f;
};

Blow strike(walker* a, walker* b, float damage)
{
    b->stats()->set_hitpoints(1000.0f);
    const float before = b->stats()->hitpoints();
    a->set_damage(damage);
    Blow out;
    out.landed = a->attack(b);
    out.hp_lost = before - b->stats()->hitpoints();
    return out;
}

// The alliance a charmed troop answers with, on one value of the setting:
// `charmed_side` true is the charmer's side (the fix), false the tent's
// (the classic game).
void expect_alliance(CharmScene& s, walker* charmer, bool charmed_side,
                     const char* what)
{
    const int on = charmed_side ? 1 : 0;
    const int off = charmed_side ? 0 : 1;
    EXPECT_EQ(on, s.red->is_friendly(s.troop)) << what << ": red asks of the troop";
    EXPECT_EQ(on, s.troop->is_friendly(s.red)) << what << ": the troop asks of red";
    EXPECT_EQ(on, charmer->is_friendly(s.troop)) << what << ": the charmer asks";
    EXPECT_EQ(off, s.troop->is_friendly(s.green)) << what << ": the troop asks of green";
    EXPECT_EQ(off, s.green->is_friendly(s.troop)) << what << ": green asks of the troop";
    EXPECT_EQ(on, s.troop->is_friendly_to_team(kRed)) << what << ": friendly to team 0";
    EXPECT_EQ(off, s.troop->is_friendly_to_team(kGreen)) << what << ": friendly to team 1";
}

// The whole check after a charm landed on the troop, then the charm's end.
void check_charmed_troop(CharmScene& s, walker* charmer, short fixes,
                         std::int32_t lifetime_before)
{
    const bool fixed = fixes != 0;
    ASSERT_EQ(kRed, s.troop->team_num()) << "the charm landed";
    ASSERT_EQ(kGreen, s.troop->real_team_num());
    ASSERT_GT(s.troop->charm_left(), 0);
    expect_alliance(s, charmer, fixed, "charmed");

    // The charm is not the tent tie's business.
    EXPECT_EQ(s.tent, s.troop->owner());
    EXPECT_EQ(lifetime_before, s.troop->lifetime());

    // Red's blow on its own charmed troop: refused with the fix, lands
    // in the classic game.
    const Blow red_hits = strike(s.red, s.troop, 40.0f);
    EXPECT_EQ(!fixed, red_hits.landed);
    if (fixed)
        EXPECT_EQ(0.0f, red_hits.hp_lost);
    else
        EXPECT_GT(red_hits.hp_lost, 0.0f);

    // The troop's blow on green: lands with the fix, and red (the player's
    // team, the colour the troop wears) is paid the classic team score
    // through attack()'s own climb; refused in the classic game.
    const std::uint32_t score_before = s.world().m_score[kRed];
    const Blow troop_hits = strike(s.troop, s.green, 40.0f);
    EXPECT_EQ(fixed, troop_hits.landed);
    if (fixed)
    {
        ASSERT_GT(troop_hits.hp_lost, 0.0f);
        EXPECT_EQ(score_before + static_cast<std::uint32_t>(troop_hits.hp_lost) +
                      static_cast<std::uint32_t>(s.green->stats()->level()),
                  s.world().m_score[kRed])
            << "the troop scores for the colour it wears";
        EXPECT_EQ(s.troop->entity_id(), s.green->last_attacker_id());
        EXPECT_EQ(kRed, s.green->last_attacker_team());
    }
    else
    {
        EXPECT_EQ(0.0f, troop_hits.hp_lost);
        EXPECT_EQ(score_before, s.world().m_score[kRed]);
    }

    // The troop's blow on red: refused with the fix; the classic troop
    // fights its charmer's side.
    const Blow troop_hits_red = strike(s.troop, s.red, 1.0f);
    EXPECT_EQ(!fixed, troop_hits_red.landed);

    // The charm runs out: the troop is green again, the tent's, on both
    // values.
    s.troop->set_charm_left(1);
    s.world().tick();
    ASSERT_FALSE(s.troop->dead());
    EXPECT_EQ(kGreen, s.troop->team_num());
    EXPECT_EQ(255, s.troop->real_team_num());
    EXPECT_EQ(0, s.troop->charm_left());
    EXPECT_EQ(s.tent, s.troop->owner());
    EXPECT_EQ(0, s.red->is_friendly(s.troop));
    EXPECT_EQ(1, s.troop->is_friendly(s.green));
    EXPECT_EQ(1, s.troop->is_friendly_to_team(kGreen));
}

}  // namespace

// A level-7 thief's CHARM OPPONENT (slot 3, Shift) on a green tent's troop
// 30 px away.
// RED on the classic climbs: with the setting on red still sees a foe,
// red's blow lands and the troop refuses green.
TEST(GameplayFixesCharm, a_charmed_tent_troop_fights_for_its_charmer)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        ASSERT_NE(nullptr, s.troop) << "the tent spawned";
        ASSERT_EQ(s.tent, s.troop->owner());
        ASSERT_EQ(811, s.troop->lifetime()) << "800 + 11 x the tent's level";
        walker* thief = s.add_caster(FAMILY_THIEF, 7, 3, 1);
        ASSERT_NE(nullptr, thief);
        ASSERT_TRUE(thief->special());
        check_charmed_troop(s, thief, fixes, 811);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A level-10 archmage's MIND CONTROL (slot 4) on the same troop.
// RED on the classic climbs, as above.
TEST(GameplayFixesCharm, a_mind_controlled_tent_troop_fights_for_its_master)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        ASSERT_NE(nullptr, s.troop);
        walker* archmage = s.add_caster(FAMILY_ARCHMAGE, 10, 4, 0);
        ASSERT_NE(nullptr, archmage);
        ASSERT_TRUE(archmage->special());
        check_charmed_troop(s, archmage, fixes, 811);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Not only a tent's: a summon an enemy (green) archmage owns, charmed by
// the thief, heads its own chain too, and a kill or a blow it lands is its
// own (walker::credit_head stops at the charm), not its summoner's.
// RED on the classic climbs: the summon still answers for the summoner.
TEST(GameplayFixesCharm, a_charmed_summon_of_an_enemy_caster_fights_for_its_charmer)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        ASSERT_NE(nullptr, s.troop);
        walker* summoner = s.add(FAMILY_ARCHMAGE, kGreen, 400, 100);
        ASSERT_NE(nullptr, summoner);
        s.troop->set_owner(summoner);
        walker* thief = s.add_caster(FAMILY_THIEF, 7, 3, 1);
        ASSERT_NE(nullptr, thief);
        ASSERT_TRUE(thief->special());
        ASSERT_EQ(kRed, s.troop->team_num()) << "the charm landed";
        ASSERT_GT(s.troop->charm_left(), 0);

        const bool fixed = fixes != 0;
        expect_alliance(s, thief, fixed, "charmed summon");
        EXPECT_EQ(fixed ? 0 : 1, s.troop->is_friendly(summoner));
        EXPECT_EQ(fixed ? 0 : 1, summoner->is_friendly(s.troop));
        EXPECT_EQ(summoner, s.troop->owner()) << "the owner is not touched";

        // Its blow on green lands with the fix and is credited to the
        // summon itself, in red; the classic summon refuses green.
        const Blow hits_green = strike(s.troop, s.green, 40.0f);
        EXPECT_EQ(fixed, hits_green.landed);
        EXPECT_EQ(fixed ? s.troop->entity_id() : 0u, s.green->last_attacker_id());
        EXPECT_EQ(fixed ? std::int16_t{kRed} : std::int16_t{-1},
                  s.green->last_attacker_team());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The control: an uncharmed troop climbs to its tent on both values, and a
// real_team_num written with no charm behind it (the parity runners, mapgen
// and level hooks write it for a fresh spawn) does not make a head: the
// charm's clock is the test.
TEST(GameplayFixesCharm, an_uncharmed_tent_troop_answers_for_its_tent_on_both_values)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        ASSERT_NE(nullptr, s.troop);
        s.troop->set_real_team_num(kGreen);
        ASSERT_EQ(0, s.troop->charm_left());
        EXPECT_EQ(0, s.red->is_friendly(s.troop));
        EXPECT_EQ(1, s.troop->is_friendly(s.green));
        EXPECT_EQ(1, s.green->is_friendly(s.troop));
        EXPECT_EQ(1, s.troop->is_friendly_to_team(kGreen));
        EXPECT_EQ(0, s.troop->is_friendly_to_team(kRed));
        const Blow red_hits = strike(s.red, s.troop, 40.0f);
        EXPECT_TRUE(red_hits.landed);
        EXPECT_EQ(s.red->entity_id(), s.troop->last_attacker_id());
        const Blow troop_hits_green = strike(s.troop, s.green, 40.0f);
        EXPECT_FALSE(troop_hits_green.landed);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
