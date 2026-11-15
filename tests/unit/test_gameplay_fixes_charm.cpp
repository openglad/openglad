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
//
// Gameplay Fixes AP (#429), GameplayFixesChain below: one climb for every
// owner chain. Classic, friendliness stops at a dead owner while the climb
// a blow scores by (walker::attack) and the kill credit go through it; with
// the setting on they read walker::chain_head, through a dead owner and
// stopping at a charmed walker.
//
// Gameplay Fixes AZ (#502): with the setting on friendliness reads each
// walker's own colour; the climb still names the person behind a blow (the
// score gate's hero, experience, credit).

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

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
// With the setting on this holds by fix AZ (the troop wears red and fights for
// it) and by fix AD (the climb stops at it) alike, and a tent's troop is its
// own credit, so breaking either alone does not fail it; fix AD's stop is
// pinned by a_charmed_summon_of_an_enemy_caster_fights_for_its_charmer,
// GameplayFixesChain.a_troop_that_dies_charmed_heads_its_chain_until_the_tick_ends,
// ModesTdm.a_charmed_summons_kill_credits_its_charmed_colour and
// ModesMutant.the_damage_gate_reads_the_games_owner_climb. With the setting off
// it pins the classic climb through the charm.
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
// With the setting on this holds by fix AZ and by fix AD alike, as above;
// with the setting off it pins the classic climb through the charm.
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
// With the setting on its credit leg (`last_attacker_id` is the summon) is
// what fails when fix AD's stop is broken.
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

// The control: an uncharmed troop answers for the colour it wears, which is
// its tent's (classic by the climb to its tent; with the setting on by its own
// colour, fix AZ), and a real_team_num written with no charm behind it (the
// parity runners, mapgen and level hooks write it for a fresh spawn) does not
// make a head: the charm's clock is the test.
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

// ---------------------------------------------------------------------------
// Fix AP: one owner-chain climb
// ---------------------------------------------------------------------------

namespace {

// A green caster C whose summon S (green, owner C) has thrown a knife W
// (green, owner S). Then red charms C, as CHARM leaves a walker: red colour,
// green real team, a running charm clock. The summon and the knife were
// made before the charm, so they still wear green.
struct DeadCasterChain
{
    walker* caster = nullptr;
    walker* summon = nullptr;
    walker* knife = nullptr;
};

DeadCasterChain charmed_caster_chain(CharmScene& s)
{
    DeadCasterChain c;
    c.caster = s.add(FAMILY_ARCHMAGE, kGreen, 400, 100);
    c.summon = s.add(FAMILY_SKELETON, kGreen, 420, 100);
    if (c.caster == nullptr || c.summon == nullptr)
        return c;
    c.summon->set_owner(c.caster);
    c.knife = s.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    if (c.knife == nullptr)
        return c;
    c.knife->set_team_num(kGreen);
    c.knife->set_owner(c.summon);
    c.knife->stats()->set_level(4);
    c.knife->stats()->set_hitpoints(1000.0f);
    c.caster->set_team_num(kRed);
    c.caster->set_real_team_num(kGreen);
    c.caster->set_charm_left(30);
    return c;
}

std::array<std::uint32_t, 4> scores(CharmScene& s)
{
    std::array<std::uint32_t, 4> out{};
    for (std::size_t i = 0; i < out.size(); ++i)
        out[i] = s.world().m_score[i];
    return out;
}

}  // namespace

// The charmed caster dies, and within that tick (before the tick's end
// drops every link to the dead) the knife its summon threw asks whose side
// it is on. Both values: the climb a blow scores by and the kill credit go
// through the dead caster to it. Off: friendliness stops at the dead caster,
// so the knife and the summon answer for the summon's own colour (green):
// the knife's blow on red lands and is credited to red, the caster's colour,
// scoring nothing (the head's team is the victim's), and its blow on green is
// refused. On (Gameplay Fixes AZ): every walker fights for the colour it
// wears: the knife and the summon are green, the knife's blow on red lands,
// credited to the caster (the person behind it) with the knife's colour,
// green, and its blow on green is refused. Nobody is paid: the head has no
// record and green is not the player's team.
// Break: alliance_head's `return this;` -> `return chain_head();` (the
// setting-on arm answers red, the dead caster's colour).
TEST(GameplayFixesChain, a_dead_charmed_casters_summon_and_knife_fight_for_the_colour_they_wear_with_the_setting_on)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        DeadCasterChain c = charmed_caster_chain(s);
        ASSERT_TRUE(c.caster && c.summon && c.knife);
        c.caster->set_dead(1);

        EXPECT_EQ(c.caster, c.knife->chain_head()) << "the climb goes through the dead caster";
        EXPECT_EQ(c.caster, c.summon->chain_head());
        EXPECT_EQ(c.caster, c.knife->credit_head()) << "the credit is the topmost living, the caster";

        EXPECT_EQ(0, c.knife->is_friendly(s.red));
        EXPECT_EQ(1, c.knife->is_friendly(s.green));
        EXPECT_EQ(0, c.knife->is_friendly_to_team(kRed));
        EXPECT_EQ(1, c.knife->is_friendly_to_team(kGreen));
        EXPECT_EQ(0, c.summon->is_friendly(s.red));
        EXPECT_EQ(0, s.red->is_friendly(c.summon));
        EXPECT_EQ(1, c.summon->is_friendly_to_team(kGreen));

        const auto before = scores(s);
        const Blow on_red = strike(c.knife, s.red, 40.0f);
        EXPECT_TRUE(on_red.landed);
        EXPECT_GT(on_red.hp_lost, 0.0f) << "the knife fights for green";
        EXPECT_EQ(c.caster->entity_id(), s.red->last_attacker_id());
        EXPECT_EQ(fixes != 0 ? std::int16_t{kGreen} : std::int16_t{kRed}, s.red->last_attacker_team())
            << (fixes != 0 ? "setting on: the knife's own colour"
                           : "setting off: the credited caster's colour");
        EXPECT_EQ(before, scores(s)) << "no record and not the player's team: no score";

        const Blow on_green = strike(c.knife, s.green, 40.0f);
        EXPECT_FALSE(on_green.landed) << "green is the knife's own side";
        EXPECT_EQ(0.0f, on_green.hp_lost);
        EXPECT_EQ(-1, s.green->last_attacker_team());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Gameplay Fixes AZ (#502): the maintainer's example. The summon's knife was
// thrown in green before its caster was charmed red; the caster, alive, has
// a hero record, which opens the score gate on both values. With the setting
// on the knife fights and pays for the colour it wears: its blow on red lands
// and pays green its damage d plus red's level L; the kill pays that blow and
// d plus 10 L more, to green; the kill stamp names the caster (the person
// behind it) and green; its blow on green is refused. Classic, the knife
// answers for its chain head, the charmed caster, red: its blow on red is
// refused, its blow on green lands and pays green (a classic payment reads
// the hitter's own colour), and the stamp is the caster's colour, red.
// Breaks: score_team's on arm `playerteam` -> `headguy->team_num()` (on:
// red is paid); alliance_head's `return this;` -> `return chain_head();`
// (on: the blow on red is refused).
TEST(GameplayFixesChain, a_shot_thrown_before_its_caster_was_charmed_hits_and_pays_its_own_colour)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        DeadCasterChain c = charmed_caster_chain(s);
        ASSERT_TRUE(c.caster && c.summon && c.knife);
        c.caster->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHMAGE));
        const bool fixed = fixes != 0;
        walker* const foe = fixed ? s.red : s.green;
        walker* const friend_ = fixed ? s.green : s.red;
        const auto L = static_cast<std::uint32_t>(foe->stats()->level());

        const auto before = scores(s);
        const Blow refused = strike(c.knife, friend_, 40.0f);
        EXPECT_FALSE(refused.landed) << "the knife's own side";
        EXPECT_EQ(before, scores(s));

        const Blow blow = strike(c.knife, foe, 40.0f);
        ASSERT_TRUE(blow.landed);
        ASSERT_GT(blow.hp_lost, 0.0f);
        const auto d = static_cast<std::uint32_t>(blow.hp_lost);
        auto expected = before;
        expected[kGreen] += d + L;
        EXPECT_EQ(expected, scores(s)) << "green, the knife's colour, is paid, red is not";
        EXPECT_EQ(c.caster->entity_id(), foe->last_attacker_id());
        EXPECT_EQ(fixed ? std::int16_t{kGreen} : std::int16_t{kRed}, foe->last_attacker_team());

        // The kill: the same blow (same damage, same armour, the fixed test
        // random source) on a victim at 1 hp.
        foe->stats()->set_hitpoints(1.0f);
        c.knife->set_damage(40.0f);
        ASSERT_TRUE(c.knife->attack(foe));
        ASSERT_EQ(1, foe->dead()) << "the blow kills";
        expected[kGreen] += (d + L) + (d + 10 * L);
        EXPECT_EQ(expected, scores(s));
        EXPECT_EQ(fixed ? std::int16_t{kGreen} : std::int16_t{kRed}, foe->last_attacker_team());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// #505: a caster that dies charmed. The green caster (charmed red, no owner)
// dies; in the same tick its live summon (green, made before the charm)
// strikes red, the charmer's team. On both values the summon fights for the
// colour it wears, green, so the blow lands: classic friendliness stops at the
// dead owner (the summon stands for its own colour), and with the setting on
// (Gameplay Fixes AZ) friendliness does not climb at all. The caster's charm
// clock decides nothing about sides, so taking it away changes nothing.
// Break: alliance_head's `return this;` -> `return chain_head();` (on: the
// summon answers for red and the blow is refused).
TEST(GameplayFixesChain, a_summon_of_a_caster_that_died_charmed_fights_for_its_own_colour)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        DeadCasterChain c = charmed_caster_chain(s);
        ASSERT_TRUE(c.caster && c.summon && c.knife);
        c.caster->set_dead(1);
        for (const short clock : {short{30}, short{0}})
        {
            SCOPED_TRACE(testing::Message() << "charm clock " << clock);
            c.caster->set_charm_left(clock);
            EXPECT_EQ(c.caster, c.summon->chain_head()) << "the caster has no owner";
            EXPECT_EQ(0, c.summon->is_friendly(s.red));
            EXPECT_EQ(0, c.summon->is_friendly_to_team(kRed));
            const Blow on_red = strike(c.summon, s.red, 40.0f);
            EXPECT_TRUE(on_red.landed);
            EXPECT_GT(on_red.hp_lost, 0.0f) << "the summon is green";
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// #505: a tent troop that dies charmed keeps heading its chain until the
// tick ends. The troop is charmed red by hand (the charmer's colour, its
// tent's real team, a running clock) and owns a red knife. With the setting
// on the troop heads the knife's chain alive and dead (fix AD): it is the
// person behind the knife's blow. The knife wears red, so with the setting on
// (Gameplay Fixes AZ) its blow on green pays red, its side and the player's
// team. Classic, the climb goes on to the green tent, whose colour is not the
// player's team: no score. On both values the credit is the troop (the
// topmost living) and the knife is red's friend.
// Break: charmed_head's clock arm gains `&& dead() == 0` (a dead walker is
// never charmed: with the setting on the head becomes the tent). The score
// alone does not tell that break apart from fix AZ (the knife and the troop
// are red alike), so the chain_head lines catch it.
TEST(GameplayFixesChain, a_troop_that_dies_charmed_heads_its_chain_until_the_tick_ends)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        ASSERT_NE(nullptr, s.troop);
        const bool fixed = fixes != 0;
        s.troop->set_real_team_num(kGreen);
        s.troop->set_team_num(kRed);
        s.troop->set_charm_left(30);
        walker* k = s.world().add_weap_ob(Order::Weapon, FAMILY_KNIFE);
        ASSERT_NE(nullptr, k);
        k->set_team_num(kRed);
        k->set_owner(s.troop);
        k->stats()->set_level(4);
        k->stats()->set_hitpoints(1000.0f);

        EXPECT_EQ(fixed ? s.troop : s.tent, k->chain_head()) << "alive";
        s.troop->set_dead(1);
        EXPECT_EQ(fixed ? s.troop : s.tent, k->chain_head()) << "dead, this tick";
        EXPECT_EQ(s.troop, k->credit_head()) << "the topmost living";
        EXPECT_EQ(1, k->is_friendly(s.red));

        const auto L = static_cast<std::uint32_t>(s.green->stats()->level());
        const auto before = scores(s);
        const Blow blow = strike(k, s.green, 40.0f);
        ASSERT_TRUE(blow.landed);
        ASSERT_GT(blow.hp_lost, 0.0f);
        const auto d = static_cast<std::uint32_t>(blow.hp_lost);
        auto expected = before;
        if (fixed)
            expected[kRed] += d + L;
        EXPECT_EQ(expected, scores(s))
            << (fixed ? "setting on: red, the knife's colour and the player's team, scores"
                      : "setting off: the head is the green tent: no score");
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The same chain with the charmed caster alive. On both values the climb and
// the credit name the caster. Classic, friendliness climbs to it too, so the
// knife and the summon answer for red, its colour now. With the setting on
// (Gameplay Fixes AZ) they answer for the colour they wear, green: the
// knife's blow on green is refused and its blow on red lands, stamped green.
// Break: alliance_head's `return this;` -> `return chain_head();`.
TEST(GameplayFixesChain, a_live_charmed_casters_summon_fights_for_its_own_colour_with_the_setting_on)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        DeadCasterChain c = charmed_caster_chain(s);
        ASSERT_TRUE(c.caster && c.summon && c.knife);
        const bool fixed = fixes != 0;
        EXPECT_EQ(c.caster, c.knife->chain_head());
        EXPECT_EQ(c.caster, c.knife->credit_head());
        EXPECT_EQ(fixed ? 0 : 1, c.knife->is_friendly(s.red));
        EXPECT_EQ(fixed ? 1 : 0, c.knife->is_friendly(s.green));
        EXPECT_EQ(fixed ? 0 : 1, c.summon->is_friendly_to_team(kRed));
        EXPECT_EQ(fixed ? 1 : 0, c.summon->is_friendly_to_team(kGreen));
        walker* const foe = fixed ? s.red : s.green;
        walker* const friend_ = fixed ? s.green : s.red;
        EXPECT_FALSE(strike(c.knife, friend_, 40.0f).landed);
        const Blow on_foe = strike(c.knife, foe, 40.0f);
        EXPECT_TRUE(on_foe.landed);
        EXPECT_EQ(fixed ? std::int16_t{kGreen} : std::int16_t{kRed}, foe->last_attacker_team());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

namespace {

// A walker charmed by hand with the charm's own writes
// (living-11-thief.lua: its colour remembered, the charmer's team, a clock).
void charm_by_hand(walker* w, unsigned char onto)
{
    w->set_real_team_num(w->team_num());
    w->set_team_num(onto);
    w->set_charm_left(30);
}

// The charm's end, with living::act's own writes when the clock runs out
// (living.cpp, "Charmed-ness"): back to the colour remembered.
void uncharm_by_hand(walker* w)
{
    w->set_charm_left(0);
    w->set_team_num(w->real_team_num());
    w->set_real_team_num(255);
}

// A green caster with magic to spare, seated so its cast draws nothing extra.
walker* add_green_caster(CharmScene& s, int family, short level, char slot, int x, int y)
{
    walker* c = s.add(family, kGreen, x, y);
    if (c == nullptr)
        return nullptr;
    c->stats()->set_level(level);
    c->stats()->set_max_magicpoints(1000.0f);
    c->stats()->set_magicpoints(1000.0f);
    c->stats()->set_magic_per_round(0.0f);
    c->set_busy(0);
    c->set_user(0);
    c->set_current_special(slot);
    c->set_shifter_down(0);
    return c;
}

std::vector<walker*> live(GameWorld& w, Order order, int family)
{
    std::vector<walker*> out;
    const auto& list = order == Order::Weapon ? w.weaplist : w.oblist;
    for (const auto& ob : list)
        if (ob && !ob->dead() && ob->query_order() == order && ob->family() == family)
            out.push_back(ob.get());
    return out;
}

}  // namespace

// The maintainer ruled (2026-11-15) that a summon stays on the team it was
// summoned for. A green druid casts SUMMON FAERIE, then is charmed red. With
// the setting on (Gameplay Fixes AZ) the faerie fights for green, the colour
// it was made with: it is
// the charmed druid's foe now, its blow on a green soldier is refused and
// its blow on a red one lands. Classic, it fights for its druid's colour of
// the moment, red: the reverse.
// Break: alliance_head's `return this;` -> `return chain_head();`.
TEST(GameplayFixesChain, a_summon_keeps_its_colour_when_its_caster_is_charmed_after_the_summoning)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        walker* druid = add_green_caster(s, FAMILY_DRUID, 4, 2, 200, 200);
        ASSERT_NE(nullptr, druid);
        std::string reason;
        ASSERT_TRUE(druid->special(nullptr, &reason)) << reason;
        const auto faeries = live(s.world(), Order::Living, FAMILY_FAERIE);
        ASSERT_EQ(1u, faeries.size());
        walker* faerie = faeries.front();
        ASSERT_EQ(druid, faerie->owner());
        ASSERT_EQ(kGreen, faerie->team_num()) << "the druid's colour at the summoning";
        charm_by_hand(druid, kRed);

        const bool fixed = fixes != 0;
        EXPECT_EQ(fixed ? 1 : 0, faerie->is_friendly_to_team(kGreen));
        EXPECT_EQ(fixed ? 0 : 1, faerie->is_friendly(druid));
        walker* const foe = fixed ? s.red : s.green;
        walker* const friend_ = fixed ? s.green : s.red;
        EXPECT_FALSE(strike(faerie, friend_, 20.0f).landed);
        const Blow on_foe = strike(faerie, foe, 20.0f);
        EXPECT_TRUE(on_foe.landed);
        EXPECT_GT(on_foe.hp_lost, 0.0f);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The maintainer ruled (2026-11-15) that a tent's troop stays on the team the
// tent was on when it made it. A script recolours the green tent red (what
// walker:set_team_num does).
// With the setting on (Gameplay Fixes AZ) the troop fights for green, the
// colour it wears: red is its foe and its blow on red lands, and the tent,
// red now, is its foe too (a troop is a living, never a rider). Classic, the
// troop follows its tent's colour: its blow on red is refused and green sees
// a foe in it.
// Break: alliance_head's `return this;` -> `return chain_head();`.
TEST(GameplayFixesChain, a_tent_troop_keeps_its_colour_when_a_script_recolours_its_tent)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        ASSERT_NE(nullptr, s.troop);
        ASSERT_EQ(kGreen, s.troop->team_num());
        s.tent->set_team_num(kRed);
        const bool fixed = fixes != 0;
        EXPECT_EQ(fixed ? 1 : 0, s.troop->is_friendly_to_team(kGreen));
        EXPECT_EQ(fixed ? 0 : 1, s.red->is_friendly(s.troop));
        EXPECT_EQ(fixed ? 1 : 0, s.green->is_friendly(s.troop));
        EXPECT_EQ(fixed ? 0 : 1, s.troop->is_friendly(s.tent));
        const Blow on_red = strike(s.troop, s.red, 40.0f);
        EXPECT_EQ(fixed, on_red.landed);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Gameplay Fixes AZ (#502), the maintainer's example: a green hero archer
// fires an arrow (fire() stamps it green), then is charmed red while it
// flies. With the setting on the arrow fights for green: red's spot blocks it
// and green's lets it pass (the obmap's weapon pass rule asks is_friendly),
// its blow on red lands and pays green d + L (the archer's record opens the
// gate), stamped green and credited to the archer, and its blow on green is
// refused. A shot does not ride its thrower (only a KIT_RIDER thing does), so
// it and the charmed archer are foes and its blow on him lands. Classic, the
// arrow answers for its archer, red now: it passes red, green blocks it, its
// blow on red is refused, its blow on green lands and pays green (a classic
// payment reads the hitter's own colour), and it is the archer's friend.
// Breaks: alliance_head's `return this;` -> `return chain_head();`; a fired
// shot marked KIT_RIDER (fire() sets the mark on every weapon it makes: the
// arrow and the archer become friends with the setting on).
TEST(GameplayFixesChain, an_arrow_in_flight_keeps_the_colour_it_was_shot_in_when_its_archer_is_charmed)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        walker* archer = s.add(FAMILY_ARCHER, kGreen, 300, 100);
        ASSERT_NE(nullptr, archer);
        archer->set_owned_myguy(std::make_unique<guy>(FAMILY_ARCHER));
        archer->stats()->set_max_magicpoints(100.0f);
        archer->stats()->set_magicpoints(100.0f);
        archer->set_lastx(1.0f);
        archer->set_lasty(0.0f);
        walker* arrow = archer->fire();
        ASSERT_NE(nullptr, arrow);
        ASSERT_EQ(Order::Weapon, arrow->query_order());
        ASSERT_EQ(kGreen, arrow->team_num()) << "fire() stamps the archer's colour";
        ASSERT_EQ(archer, arrow->owner());
        arrow->stats()->set_hitpoints(1000.0f);
        arrow->setxy(160, 300);
        charm_by_hand(archer, kRed);

        const bool fixed = fixes != 0;
        EXPECT_EQ(!fixed, s.world().query_object_passable(
                              static_cast<float>(s.red->xpos()), static_cast<float>(s.red->ypos()), arrow))
            << "red's spot";
        EXPECT_EQ(fixed, s.world().query_object_passable(
                             static_cast<float>(s.green->xpos()), static_cast<float>(s.green->ypos()), arrow))
            << "green's spot";
        EXPECT_EQ(fixed ? 0 : 1, arrow->is_friendly(archer)) << "a shot does not ride its thrower";
        EXPECT_EQ(fixed ? 0 : 1, archer->is_friendly(arrow));

        walker* const foe = fixed ? s.red : s.green;
        walker* const friend_ = fixed ? s.green : s.red;
        const auto L = static_cast<std::uint32_t>(foe->stats()->level());
        const auto before = scores(s);
        EXPECT_FALSE(strike(arrow, friend_, 40.0f).landed);
        const Blow blow = strike(arrow, foe, 40.0f);
        ASSERT_TRUE(blow.landed);
        ASSERT_GT(blow.hp_lost, 0.0f);
        auto expected = before;
        expected[kGreen] += static_cast<std::uint32_t>(blow.hp_lost) + L;
        EXPECT_EQ(expected, scores(s)) << "green, the arrow's colour, is paid";
        EXPECT_EQ(archer->entity_id(), foe->last_attacker_id());
        EXPECT_EQ(fixed ? std::int16_t{kGreen} : std::int16_t{kRed}, foe->last_attacker_team());

        const Blow on_archer = strike(arrow, archer, 40.0f);
        EXPECT_EQ(fixed, on_archer.landed) << "with the setting on the arrow is its charmed archer's foe";
        if (fixed)
            EXPECT_GT(on_archer.hp_lost, 0.0f);
        else
            EXPECT_EQ(0.0f, on_archer.hp_lost);
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

namespace {

// A green level-6 druid at (200, 200) casts CIRCLE OF PROTECTION (slot 4) on
// the one green walker within its 60 px, `friend_` (red and green are sent
// far off first). Answers the one ring, or nullptr.
walker* ring_on(CharmScene& s, walker* druid, walker* friend_)
{
    s.red->setxy(500, 500);
    s.green->setxy(600, 500);
    druid->set_busy(0);
    druid->set_current_special(4);
    if (!druid->special(nullptr, nullptr))
        return nullptr;
    const auto rings = live(s.world(), Order::Weapon, FAMILY_CIRCLE_PROTECTION);
    if (rings.size() != 1 || rings.front()->owner() != friend_)
        return nullptr;
    return rings.front();
}

// Whether `w` may stand on the ring's spot (rx, ry): the obmap's weapon pass
// rule asks the ring's side (it blocks a foe, lets a friend pass).
bool passes(CharmScene& s, float rx, float ry, walker* w)
{
    return s.world().query_object_passable(rx, ry, w);
}

}  // namespace

// Gameplay Fixes AZ (#502): a thing that rides a walker goes with him (the
// maintainer: "obviously they should go with the walker"). A green level-6
// druid casts CIRCLE OF PROTECTION on the one green soldier within its 60 px;
// the ring is the soldier's (owner and colour) and, with the setting on,
// carries KIT_RIDER. The soldier is charmed red and steps away (the ring
// stays where it was, no tick re-centres it). Whom the ring blocks (the
// obmap's weapon pass rule; the ring deals no damage, so no hit point is
// read): never its walker; it lets red pass and blocks green, the soldier's
// side of the moment, and so does attack's answer (it may strike green, never
// red or its walker). A knife the charmed soldier throws for his new side is
// the ring's friend. When the charm ends the ring turns back with him: green
// passes and red is blocked. Classic one owner chain gives the same answers.
// Breaks: alliance_head's rider line `return ridden;` -> `return this;` (on:
// the ring keeps green after the charm: it blocks red and its walker and lets
// green pass); the druid's `gf.rides(...)` wrapper removed at
// living-13-druid.lua (the same, and the mark is 0).
TEST(GameplayFixesChain, a_ring_of_protection_goes_with_its_walker_and_turns_back_with_him)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        walker* druid = add_green_caster(s, FAMILY_DRUID, 6, 4, 200, 200);
        ASSERT_NE(nullptr, druid);
        walker* soldier = s.add(FAMILY_SOLDIER, kGreen, 220, 200);
        ASSERT_NE(nullptr, soldier);
        walker* ring = ring_on(s, druid, soldier);
        ASSERT_NE(nullptr, ring) << "one friend in reach, one ring, his";
        ASSERT_EQ(kGreen, ring->team_num());
        const bool fixed = fixes != 0;
        EXPECT_EQ(fixed ? KIT_RIDER : 0, ring->kit_state() & KIT_RIDER);

        charm_by_hand(soldier, kRed);
        const float rx = static_cast<float>(ring->xpos());
        const float ry = static_cast<float>(ring->ypos());
        soldier->setxy(400, 200);
        druid->setxy(100, 400);

        EXPECT_EQ(kRed, ring->fights_for_team()) << "charmed: the ring fights for red";
        EXPECT_EQ(1, ring->is_friendly(soldier));
        EXPECT_EQ(1, soldier->is_friendly(ring));
        EXPECT_TRUE(passes(s, rx, ry, soldier)) << "its walker is never blocked";
        EXPECT_TRUE(passes(s, rx, ry, s.red)) << "charmed: red, his side now, passes";
        EXPECT_FALSE(passes(s, rx, ry, s.green)) << "charmed: green, his old side, is blocked";

        ring->set_damage(0.0f);
        EXPECT_FALSE(ring->attack(soldier)) << "never its walker";
        ring->set_damage(0.0f);
        EXPECT_FALSE(ring->attack(s.red)) << "charmed: red is a friend";
        ring->set_damage(0.0f);
        EXPECT_TRUE(ring->attack(s.green)) << "charmed: green is a foe";

        soldier->stats()->set_max_magicpoints(100.0f);
        soldier->stats()->set_magicpoints(100.0f);
        soldier->set_lastx(1.0f);
        soldier->set_lasty(0.0f);
        walker* knife = soldier->fire();
        ASSERT_NE(nullptr, knife);
        ASSERT_EQ(kRed, knife->team_num()) << "thrown after the charm";
        EXPECT_EQ(1, knife->is_friendly(ring)) << "his new knife is the ring's friend";
        knife->set_dead(1);

        uncharm_by_hand(soldier);
        ASSERT_EQ(kGreen, soldier->team_num());
        EXPECT_EQ(kGreen, ring->fights_for_team()) << "the charm over: the ring fights for green again";
        EXPECT_TRUE(passes(s, rx, ry, soldier)) << "its walker is never blocked";
        EXPECT_TRUE(passes(s, rx, ry, s.green)) << "the charm over: green passes again";
        EXPECT_FALSE(passes(s, rx, ry, s.red)) << "the charm over: red is blocked again";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Gameplay Fixes AZ (#502): a rider goes with the walker it rides, one hop
// and no further. A green level-6 druid summons a faerie (slot 2) and then
// rings it (slot 4): the ring's walker is the faerie, the druid's summon. The
// druid is charmed red. With the setting on the faerie fights for green, the
// colour it was summoned for, and so does its ring: it lets green pass and
// blocks red, never its faerie. Classic the faerie follows its charmed druid
// and so does the ring: red passes and green is blocked.
// Break: alliance_head's rider line `return ridden;` -> `return
// chain_head();` (on: the ring climbs past its faerie to the charmed druid
// and blocks green).
TEST(GameplayFixesChain, a_ring_on_a_summon_goes_with_the_summon_not_its_charmed_caster)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        walker* druid = add_green_caster(s, FAMILY_DRUID, 6, 2, 200, 200);
        ASSERT_NE(nullptr, druid);
        std::string reason;
        ASSERT_TRUE(druid->special(nullptr, &reason)) << reason;
        const auto faeries = live(s.world(), Order::Living, FAMILY_FAERIE);
        ASSERT_EQ(1u, faeries.size());
        walker* faerie = faeries.front();
        ASSERT_EQ(druid, faerie->owner());
        faerie->setxy(220, 200);
        walker* ring = ring_on(s, druid, faerie);
        ASSERT_NE(nullptr, ring) << "one friend in reach, one ring, the faerie's";

        charm_by_hand(druid, kRed);
        const float rx = static_cast<float>(ring->xpos());
        const float ry = static_cast<float>(ring->ypos());
        faerie->setxy(400, 200);
        druid->setxy(100, 400);

        const bool fixed = fixes != 0;
        const unsigned char side = fixed ? kGreen : kRed;
        EXPECT_EQ(side, ring->fights_for_team());
        EXPECT_EQ(1, ring->is_friendly(faerie));
        EXPECT_TRUE(passes(s, rx, ry, faerie)) << "its faerie is never blocked";
        EXPECT_EQ(fixed, passes(s, rx, ry, s.green)) << "a green walker";
        EXPECT_EQ(!fixed, passes(s, rx, ry, s.red)) << "a red walker";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Gameplay Fixes AZ (#502): a rider whose walker died answers for itself,
// the colour it was made with. A green druid rings a green soldier, the
// soldier is charmed red and then dies. In the rest of that tick (the ring
// still names him) and after the ring's next act (weap::act makes a weapon
// whose owner died its own owner; the ring then lives on where he fell, as in
// the old game) the ring fights for green: green passes, red is blocked.
// Classic the friendliness climb stops at a dead owner: the same answers.
// Break: alliance_head's rider test without `ridden->dead() == 0` (on: in the
// tick he died the ring still answers red).
TEST(GameplayFixesChain, a_ring_whose_walker_died_answers_for_the_colour_it_was_made_with)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        walker* druid = add_green_caster(s, FAMILY_DRUID, 6, 4, 200, 200);
        ASSERT_NE(nullptr, druid);
        walker* soldier = s.add(FAMILY_SOLDIER, kGreen, 220, 200);
        ASSERT_NE(nullptr, soldier);
        walker* ring = ring_on(s, druid, soldier);
        ASSERT_NE(nullptr, ring);
        charm_by_hand(soldier, kRed);
        ASSERT_EQ(kRed, ring->fights_for_team());
        const float rx = static_cast<float>(ring->xpos());
        const float ry = static_cast<float>(ring->ypos());
        soldier->setxy(400, 200);
        druid->setxy(100, 400);

        soldier->set_dead(1);
        ASSERT_EQ(soldier, ring->owner()) << "the tick he died: the ring still names him";
        EXPECT_EQ(kGreen, ring->fights_for_team()) << "his ring answers for itself";
        EXPECT_TRUE(passes(s, rx, ry, s.green));
        EXPECT_FALSE(passes(s, rx, ry, s.red));

        (void)ring->act();
        ASSERT_FALSE(ring->dead()) << "the ring outlives its walker";
        EXPECT_EQ(ring, ring->owner()) << "weap::act: a dead maker's weapon owns itself";
        EXPECT_EQ(kGreen, ring->fights_for_team()) << "a ring that owns itself answers for itself";
        const float sx = static_cast<float>(ring->xpos());
        const float sy = static_cast<float>(ring->ypos());
        EXPECT_TRUE(passes(s, sx, sy, s.green));
        EXPECT_FALSE(passes(s, sx, sy, s.red));
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Gameplay Fixes AZ (#502): the colour a rider's blow pays and its kill
// counts for is the side it fights for, its walker's now. A green soldier
// throws BOOMERANG (slot 2), a rider, and is charmed red (my_team is red).
// The boomerang strikes green, a foe now: with the setting on red is paid
// d + L, the stamp is red and the soldier is credited; then a lethal blow on
// green pays red d + L and the kill's d + 10 L and stamps red. Classic the
// gate reads the head's colour, red, and every payment the hitter's own
// colour, the boomerang's green; the stamp is the credited soldier's, red.
// Breaks: walker::attack's `playerteam = ... fights_for_team() ...` ->
// `team_num()` (on: the side is green, not the player's: nobody is paid);
// kill_stamp_team's `fights_for_team()` -> `team_num()` (on: stamped green).
TEST(GameplayFixesChain, a_riders_blow_after_the_charm_pays_and_kills_for_the_charmers_team)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        walker* soldier = add_green_caster(s, FAMILY_SOLDIER, 4, 2, 200, 200);
        ASSERT_NE(nullptr, soldier);
        std::string reason;
        ASSERT_TRUE(soldier->special(nullptr, &reason)) << reason;
        const auto boomerangs = live(s.world(), Order::FX, FAMILY_BOOMERANG);
        ASSERT_EQ(1u, boomerangs.size());
        walker* boomerang = boomerangs.front();
        ASSERT_EQ(soldier, boomerang->owner());
        ASSERT_EQ(kGreen, boomerang->team_num());
        charm_by_hand(soldier, kRed);

        const bool fixed = fixes != 0;
        const unsigned char paid = fixed ? kRed : kGreen;
        const auto L = static_cast<std::uint32_t>(s.green->stats()->level());
        auto expected = scores(s);
        const Blow blow = strike(boomerang, s.green, 40.0f);
        ASSERT_TRUE(blow.landed) << "green is the charmed soldier's foe";
        ASSERT_GT(blow.hp_lost, 0.0f);
        expected[paid] += static_cast<std::uint32_t>(blow.hp_lost) + L;
        EXPECT_EQ(expected, scores(s));
        EXPECT_EQ(std::int16_t{kRed}, s.green->last_attacker_team());
        EXPECT_EQ(soldier->entity_id(), s.green->last_attacker_id());

        s.green->stats()->set_hitpoints(1.0f);
        boomerang->set_damage(40.0f);
        ASSERT_TRUE(boomerang->attack(s.green));
        ASSERT_TRUE(s.green->dead());
        const auto d = static_cast<std::uint32_t>(1.0f - s.green->stats()->hitpoints());
        ASSERT_GT(d, 0u);
        expected[paid] += (d + L) + (d + 10 * L);
        EXPECT_EQ(expected, scores(s)) << "the blow and the kill";
        EXPECT_EQ(std::int16_t{kRed}, s.green->last_attacker_team()) << "the kill counts for red";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Gameplay Fixes AZ (#502): the cleric's glow is his
// ordinary weapon (fire() stamps it with his colour); it sits where it was
// fired and is auto-attackable, and it does not ride him. A green cleric
// fires a glow and is then charmed red. With the setting on the glow fights
// for green: it blocks the charmed cleric (the obmap's weapon pass rule) and
// he swings at it when he bumps it (living::collide arms init_fire), as with
// a tree or a bone wall of his old side. Classic it answers for the cleric,
// red now: it lets him pass and he does not swing.
// Breaks: alliance_head's `return this;` -> `return chain_head();`; a fired
// shot marked KIT_RIDER (on: the glow rides him and lets him pass).
TEST(GameplayFixesChain, a_charmed_cleric_is_blocked_by_his_own_glow_and_swings_at_it_with_the_setting_on)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}})
    {
        SCOPED_TRACE(fixes);
        CharmScene s(fixes);
        walker* cleric = s.add(FAMILY_CLERIC, kGreen, 300, 100);
        ASSERT_NE(nullptr, cleric);
        cleric->stats()->set_max_magicpoints(100.0f);
        cleric->stats()->set_magicpoints(100.0f);
        cleric->set_lastx(1.0f);
        cleric->set_lasty(0.0f);
        walker* glow = cleric->fire();
        ASSERT_NE(nullptr, glow);
        ASSERT_EQ(static_cast<std::uint8_t>(FAMILY_GLOW), glow->family());
        ASSERT_EQ(kGreen, glow->team_num()) << "fire() stamps the cleric's colour";
        ASSERT_EQ(cleric, glow->owner());
        glow->setxy(300, 300);
        charm_by_hand(cleric, kRed);

        const bool fixed = fixes != 0;
        EXPECT_EQ(fixed ? 0 : 1, glow->is_friendly(cleric));
        EXPECT_EQ(!fixed, s.world().query_object_passable(
                              static_cast<float>(glow->xpos()), static_cast<float>(glow->ypos()), cleric))
            << "the glow blocks its charmed cleric only with the setting on";

        auto* body = dynamic_cast<living*>(cleric);
        ASSERT_NE(nullptr, body);
        body->set_lastx(1.0f);
        body->set_lasty(0.0f);
        body->set_curdir(static_cast<signed char>(FACE_RIGHT));
        body->set_enddir(static_cast<char>(FACE_RIGHT));
        body->set_fire_frequency(5.0f);
        body->set_busy(0.0f);
        body->set_ani_type(static_cast<char>(ANI_WALK));
        body->collide(glow);
        EXPECT_EQ(glow, body->collide_ob());
        EXPECT_FLOAT_EQ(fixed ? 5.0f : 0.0f, body->busy()) << "the bump arms the swing only with the setting on";
        EXPECT_EQ(fixed ? ANI_ATTACK : ANI_WALK, static_cast<int>(body->ani_type()));
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
