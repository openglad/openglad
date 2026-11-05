/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Semantic tests, batch families-4: the rules the review found in living-17-archmage.lua.
// Each test is named after the rule it asserts.
//
// Every cast goes through the real walker::special() (the price gate and the
// charge included), every hit response through the family's real hook, and
// every bot gate through og::test::check_special_ai, against the shipped core
// pack in a hand-built open grass field (the GameplayFixesBots field). The
// classic arms were read against the old game at 7ff1654a: living::act (the
// view_all cadence), statistics::hit_response (FAMILY_ARCHMAGE) and
// walker::special (FAMILY_ARCHMAGE, slots 1 to 4).

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "../test_sim_random_scope.h"
#include "bot_gate_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/irandom.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/event.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/script/family_tuning.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

std::uint32_t state_where_next_is(std::uint32_t bound, std::uint32_t want)
{
    return state_where_draws_are({bound}, {want});
}

// The world-RNG state after draws of next(bound) for each bound, from `state`.
std::uint32_t state_after(std::uint32_t state, std::initializer_list<std::uint32_t> bounds)
{
    og::sim::SimRandom probe(state);
    for (std::uint32_t b : bounds)
        (void)probe.next(b);
    return probe.state_;
}

// Answers every bounded draw with `v` (clamped into range).
class ConstantRandom : public IRandom {
public:
    explicit ConstantRandom(std::uint32_t v) : v_(v) {}
    std::uint32_t next(std::uint32_t max_exclusive) override
    {
        if (max_exclusive == 0)
            return 0;
        return v_ < max_exclusive ? v_ : max_exclusive - 1;
    }

private:
    std::uint32_t v_;
};

struct Cast {
    bool ok = false;
    std::string reason;
};

class SemArchmage : public og::test::bot_gates::GameplayFixesBots {
protected:
    const FamilyDescriptor& desc() { return *get_family_descriptor(FAMILY_ARCHMAGE); }

    // A bot archmage (no seat, no character) with 400/400 hp.
    living* arch(unsigned char team, short x, short y, int level, float mp)
    {
        return body(add(FAMILY_ARCHMAGE, team, x, y, level, mp), 16, 16);
    }

    living* orc(unsigned char team, short x, short y, int level = 1)
    {
        return body(add(FAMILY_ORC, team, x, y, level), 16, 16);
    }

    static void give_character(walker* w, short intelligence)
    {
        auto g = std::make_unique<guy>(FAMILY_ARCHMAGE);
        g->intelligence = intelligence;
        g->teamnum = w->team_num();
        g->total_shots = 0;
        g->scen_shots = 0;
        w->set_owned_myguy(std::move(g));
    }

    Cast cast(walker* w, int slot, short shift)
    {
        w->set_current_special(static_cast<char>(slot));
        w->set_shifter_down(shift);
        Cast c;
        walker::SpecialFailure why = walker::SpecialFailure::None;
        c.ok = w->special(&why, &c.reason);
        return c;
    }

    void hit(walker* a, walker* attacker)
    {
        og::test::hit_response(desc(), a->stats(), attacker);
    }

    std::vector<walker*> live(Order order, int family)
    {
        std::vector<walker*> out;
        for (const GameWorld::EntityList* list :
             {&world().oblist, &world().fxlist, &world().weaplist}) {
            for (const auto& ob : *list) {
                if (ob != nullptr && ob->dead() == 0 && ob->query_order() == order &&
                    static_cast<int>(static_cast<unsigned char>(ob->family())) == family)
                    out.push_back(ob.get());
            }
        }
        return out;
    }

    int live_fx(int family) { return static_cast<int>(live(Order::FX, family).size()); }

    int live_livings()
    {
        int n = 0;
        for (const auto& ob : world().oblist)
            if (ob != nullptr && ob->dead() == 0 && ob->query_order() == Order::Living)
                n++;
        return n;
    }

    // Live livings `owner` owns (its summons and images).
    std::vector<walker*> owned_by(const walker* owner)
    {
        std::vector<walker*> out;
        for (const auto& ob : world().oblist)
            if (ob != nullptr && ob->dead() == 0 && ob->query_order() == Order::Living &&
                ob->owner() == owner)
                out.push_back(ob.get());
        return out;
    }

    int notifications(const char* needle)
    {
        int n = 0;
        for (const auto& ev : tw_->events.events())
            if (ev.kind == og::sim::EventKind::Notification &&
                ev.text.find(needle) != std::string::npos)
                n++;
        return n;
    }

    int all_notifications()
    {
        int n = 0;
        for (const auto& ev : tw_->events.events())
            if (ev.kind == og::sim::EventKind::Notification)
                n++;
        return n;
    }

    int sounds(std::uint32_t id)
    {
        int n = 0;
        for (const auto& ev : tw_->events.events())
            if (ev.kind == og::sim::EventKind::PlaySound && ev.a == id)
                n++;
        return n;
    }

    // Walls every grid tile the pixel box [x0, x1] x [y0, y1] touches.
    void wall_box(int x0, int y0, int x1, int y1)
    {
        GameWorld& w = world();
        for (int ty = y0 / GRID_SIZE; ty <= y1 / GRID_SIZE; ++ty)
            for (int tx = x0 / GRID_SIZE; tx <= x1 / GRID_SIZE; ++tx)
                w.grid.data[static_cast<std::size_t>(tx + w.grid.w * ty)] =
                    static_cast<std::uint8_t>(PIX_WALL4);
    }

    // The 5x5 tiles around tile (10, 10): every neighbour cell of a caster
    // standing at (160, 160) is walled (the a_true_summon_with_nowhere_to_
    // stand_is_cancelled staging).
    void wall_around_160()
    {
        wall_box(8 * GRID_SIZE, 8 * GRID_SIZE, 12 * GRID_SIZE + 15, 12 * GRID_SIZE + 15);
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// on_act_living (:46): the bonus sight
// ---------------------------------------------------------------------------

// :46 level >= 40 arm, and the quirk the review kept (triage: quirk). From
// level 40 the period is 1: view_all grows on EVERY act, at drawcycles no
// period of 39 or less would divide. drawcycle is an unsigned char in both
// games (walker.h; the old game's walker.h:216), and the old game's
// living::act tests the same drawcycle % period, so a period that does not
// divide 256 restarts at the wrap: 300 acts of a level-1 archmage (period 39)
// see drawcycles 0, 39 .. 234, then 0 and 39 again: 9 increments, not the 8
// an unbroken 39-tick cadence would give.
//
// RED (period 1 -> 2 at :47): Expected equality of these values: 3
// a->view_all() Which is: 0 "level 40".
TEST_F(SemArchmage, classic_view_all_grows_every_act_from_level_40_and_restarts_its_cadence_at_the_drawcycle_wrap)
{
    living* a = arch(1, 200, 200, 40, 0.0f);
    ASSERT_NE(nullptr, a);
    for (short level : {short{40}, short{45}}) {
        a->stats()->set_level(level);
        a->set_view_all(0);
        for (unsigned char dc : {static_cast<unsigned char>(1), static_cast<unsigned char>(7),
                                 static_cast<unsigned char>(39)}) {
            a->set_drawcycle(dc);
            og::test::on_act_living(desc(), a);
        }
        EXPECT_EQ(3, a->view_all()) << "level " << level << ": +1 on every act";
    }

    static_assert(std::is_same_v<decltype(a->drawcycle()), unsigned char>,
                  "the classic drawcycle is 8 bits wide");
    a->stats()->set_level(1);
    a->set_view_all(0);
    decltype(a->drawcycle()) dc = 0;
    for (int t = 0; t < 300; ++t) {
        a->set_drawcycle(dc);
        og::test::on_act_living(desc(), a);
        ++dc;
    }
    EXPECT_EQ(9, a->view_all())
        << "classic: 7 hits in the first 256 acts, then 0 and 39 again after the wrap";
}

// ---------------------------------------------------------------------------
// hit_response (:80 .. :154)
// ---------------------------------------------------------------------------

// :80 unaffordable / locked arm. A level-1 archmage has slots 0..1 only: with
// 1000 magic it still never reaches for HEARTBURST, and the slot-2 coin is
// never drawn. A level-4 archmage with 50 magic has slot 2 unlocked but
// cannot pay HEARTBURST's 80 (nor SUMMON's 500): nothing is cast, nothing
// drawn. The control (level 4, 1000 magic, coin 0) answers with one
// HEARTBURST (the classic reply ends there, fix AE). Classic order (setting
// off), the old game's statistics::hit_response.
//
// RED (the unlock bound (L+2)//3 -> (L+5)//3 at :79): Expected equality of
// these values: coin0 Which is: 1 world().rng_.state_ Which is: 1103527590
// "level 1: no slot-2 coin drawn".
TEST_F(SemArchmage, a_struck_archmage_never_answers_with_a_slot_its_level_locks_or_its_magic_cannot_pay)
{
    const std::uint32_t coin0 = state_where_next_is(2, 0);
    struct Case { int level; float mp; const char* what; };
    for (const Case c : {Case{1, 1000.0f, "level 1"}, Case{4, 50.0f, "level 4, 50 magic"}}) {
        fresh_world();
        living* a = arch(1, 200, 200, c.level, c.mp);
        living* o = orc(2, 230, 200);
        ASSERT_TRUE(a && o);
        world().rng_.state_ = coin0;
        hit(a, o);
        EXPECT_EQ(coin0, world().rng_.state_) << c.what << ": no slot-2 coin drawn";
        EXPECT_EQ(0, live_fx(FAMILY_EXPLOSION)) << c.what;
        EXPECT_EQ(0, live_fx(FAMILY_CHAIN)) << c.what;
        EXPECT_EQ(0u, owned_by(a).size()) << c.what << ": no summon";
        EXPECT_FLOAT_EQ(c.mp, a->stats()->magicpoints()) << c.what << ": nothing spent";
    }

    fresh_world();
    living* a = arch(1, 200, 200, 4, 1000.0f);
    living* o = orc(2, 230, 200);
    ASSERT_TRUE(a && o);
    world().rng_.state_ = coin0;
    hit(a, o);
    EXPECT_EQ(1, live_fx(FAMILY_EXPLOSION)) << "control: slot 2 unlocked and paid";
}

// :87 hero and monster arms. A struck hero archmage teleports below 3/5 of
// its health (59 of 100 flees, 60 does not); a monster only below 3/8 (37
// flees, 38 and 50 fight back: the attacker becomes its foe and it becomes
// the attacker's). The rand(3) is drawn only under the threshold (the C++
// && chain). Level 3: slots 0..1, so a healthy archmage casts nothing.
//
// RED (the hero's 3/5 -> 3/8 at :90): Expected equality of these values:
// ANI_TELE_OUT Which is: 2 a->ani_type() Which is: '\0' "hero at 50/100:
// teleports". RED (the monster's 3/8 -> 3/5 at :94): Expected: (ANI_TELE_OUT)
// != (a->ani_type()), actual: 2 vs '\x2' "monster at 50/100: fights".
TEST_F(SemArchmage, a_struck_hero_archmage_flees_below_three_fifths_and_a_monster_only_below_three_eighths)
{
    const std::uint32_t flee = state_where_next_is(3, 1);
    struct Case { bool hero; float hp; bool teleports; };
    for (const Case c : {Case{true, 50.0f, true}, Case{true, 59.0f, true},
                         Case{true, 60.0f, false}, Case{false, 50.0f, false},
                         Case{false, 37.0f, true}, Case{false, 38.0f, false}}) {
        fresh_world();
        living* a = arch(1, 200, 200, 3, 100.0f);
        living* o = orc(2, 240, 200);
        ASSERT_TRUE(a && o);
        if (c.hero)
            give_character(a, 50);
        a->stats()->set_max_hitpoints(100.0f);
        a->stats()->set_hitpoints(c.hp);
        world().rng_.state_ = flee;
        hit(a, o);
        const std::string what =
            std::string(c.hero ? "hero" : "monster") + " at " + std::to_string(int(c.hp)) + "/100";
        if (c.teleports) {
            EXPECT_EQ(ANI_TELE_OUT, a->ani_type()) << what << ": teleports";
            EXPECT_FLOAT_EQ(90.0f, a->stats()->magicpoints()) << what << ": TELEPORT's 10";
            EXPECT_EQ(nullptr, a->foe()) << what << ": flees, does not turn on the attacker";
            EXPECT_EQ(state_after(flee, {3}), world().rng_.state_) << what;
        } else {
            EXPECT_NE(ANI_TELE_OUT, a->ani_type()) << what << ": fights";
            EXPECT_EQ(o, a->foe()) << what << ": takes the attacker as foe";
            EXPECT_EQ(a, o->foe()) << what << ": the attacker takes it back";
            EXPECT_FLOAT_EQ(100.0f, a->stats()->magicpoints()) << what;
            EXPECT_EQ(flee, world().rng_.state_) << what << ": no rand(3) above the threshold";
        }
    }
}

// :106 same-attacker arm. Struck again by the foe it already fights, an
// archmage leaves the attacker's foe and its own range memory alone (the
// control: a new attacker is taken both ways and the memory reset to 15000).
//
// RED (`self:foe() ~= foe` -> `true` at :106): Expected equality of these
// values: nullptr o->foe() Which is: (the archmage) "same attacker".
TEST_F(SemArchmage, a_struck_archmage_already_fighting_its_attacker_keeps_its_range_memory)
{
    living* a = arch(1, 200, 200, 3, 100.0f);
    living* o = orc(2, 240, 200);
    ASSERT_TRUE(a && o);
    a->set_foe(o);
    o->set_foe(nullptr);
    a->stats()->set_current_distance(2);
    a->stats()->set_last_distance(1);
    hit(a, o);
    EXPECT_EQ(o, a->foe());
    EXPECT_EQ(nullptr, o->foe()) << "same attacker: not pointed back";
    EXPECT_EQ(2, a->stats()->current_distance()) << "same attacker: memory kept";
    EXPECT_EQ(1u, a->stats()->last_distance()) << "same attacker: memory kept";

    living* o2 = orc(2, 200, 240);
    ASSERT_NE(nullptr, o2);
    hit(a, o2);
    EXPECT_EQ(o2, a->foe()) << "control: a new attacker";
    EXPECT_EQ(a, o2->foe());
    EXPECT_EQ(15000, a->stats()->current_distance());
    EXPECT_EQ(15000u, a->stats()->last_distance());
}

// :112 slot 2 unaffordable arm (Gameplay Fixes on). A struck bot with 70
// magic (short of HEARTBURST's 80, enough for TELEPORT's 10), healthy, the
// orc 30 px off: possible[2] is false, so no coin is drawn and nothing cast.
//
// RED (`if possible[2]` -> `if true` at :117): Expected equality of these
// values: seed world().rng_.state_ "no coin".
TEST_F(SemArchmage, a_struck_bot_archmage_that_cannot_pay_heartburst_answers_nothing_with_gameplay_fixes)
{
    world().gameplay_fixes = 1;
    living* a = arch(1, 200, 200, 10, 70.0f);
    living* o = orc(2, 230, 200);
    ASSERT_TRUE(a && o);
    a->set_shifter_down(1);
    const std::uint32_t seed = state_where_next_is(2, 1);
    world().rng_.state_ = seed;
    hit(a, o);
    EXPECT_EQ(seed, world().rng_.state_) << "no coin";
    EXPECT_EQ(0, live_fx(FAMILY_EXPLOSION));
    EXPECT_EQ(0, live_fx(FAMILY_CHAIN));
    EXPECT_EQ(0u, owned_by(a).size());
    EXPECT_FLOAT_EQ(70.0f, a->stats()->magicpoints());
    EXPECT_NE(ANI_TELE_OUT, a->ani_type());
}

// :205 and :214, the classic summon-first order (setting off). The old game's
// walker::special answers 0 on every path (old game src/walker.cpp, the end
// of walker::special), so its hit response never ends because a cast worked
// (#422, Gameplay Fixes AE). (a) A summon that stands is paid (1100 - 300,
// half the spare, - 500 = 300 magic) and the reply goes on to slot 2: the
// coin is drawn (pinned to 1, CHAIN LIGHTNING), the chain and then the
// HEARTBURST are tried and both refused by the summon's 15 ticks of busy, so
// the archmage ends on slot 2 with the shift released, nothing more cast or
// spent. (b) A summon with nowhere to stand (the classic surcharge paid before
// the room check: 800 left) falls to slot 2: the coin (0) casts one HEARTBURST
// of 360 and the reply ends there, 360 magic left: no "then leave! :)"
// follow-up.
//
// RED (`self:special() and gf.on()` -> `self:special()` at :214): Expected
// equality of these values: state_after(coin1, {2}) ... world().rng_.state_
// "the summon goes on to the slot-2 coin". RED (the same at :271):
// Expected equality of these values: 1u bursts.size() Which is: 2 "one
// HEARTBURST, no follow-up".
TEST_F(SemArchmage, classic_a_struck_archmage_tries_slot_2_even_after_a_summon_that_stands)
{
    {
        const std::uint32_t coin1 = state_where_next_is(2, 1);
        living* a = arch(1, 200, 200, 10, 1100.0f);
        living* o = orc(2, 230, 200);
        ASSERT_TRUE(a && o);
        a->set_shifter_down(1);
        world().rng_.state_ = coin1;
        hit(a, o);
        EXPECT_EQ(1u, live(Order::Living, FAMILY_FIREELEMENTAL).size()) << "the summon stands";
        EXPECT_EQ(state_after(coin1, {2}), world().rng_.state_)
            << "the summon goes on to the slot-2 coin";
        EXPECT_EQ(2, static_cast<int>(a->current_special())) << "slot 2 was tried";
        EXPECT_EQ(0, a->shifter_down()) << "the chain refused: the shift released for HEARTBURST";
        EXPECT_FLOAT_EQ(15.0f, a->busy()) << "the summon's busy, which refused both";
        EXPECT_EQ(0, live_fx(FAMILY_EXPLOSION)) << "HEARTBURST refused: busy";
        EXPECT_EQ(0, live_fx(FAMILY_CHAIN)) << "CHAIN LIGHTNING refused: busy";
        EXPECT_FLOAT_EQ(300.0f, a->stats()->magicpoints()) << "only the summon is paid";
    }
    {
        fresh_world();
        wall_around_160();
        const std::uint32_t coin0 = state_where_next_is(2, 0);
        living* a = arch(1, 160, 160, 10, 1100.0f);
        living* o = orc(2, 224, 160);
        ASSERT_TRUE(a && o);
        a->set_shifter_down(1);
        world().rng_.state_ = coin0;
        hit(a, o);
        EXPECT_EQ(0u, live(Order::Living, FAMILY_FIREELEMENTAL).size()) << "no room";
        const auto bursts = live(Order::FX, FAMILY_EXPLOSION);
        ASSERT_EQ(1u, bursts.size()) << "one HEARTBURST, no follow-up";
        EXPECT_FLOAT_EQ(360.0f, bursts[0]->damage());
        EXPECT_EQ(0, live_fx(FAMILY_CHAIN));
        EXPECT_FLOAT_EQ(360.0f, a->stats()->magicpoints()) << "1100 - 300 - 360 - 80";
        EXPECT_EQ(state_after(coin0, {2}), world().rng_.state_) << "the slot-2 coin";
    }
}

// :214 and :279 with the setting on. A struck bot never reaches the classic
// order with Gameplay Fixes (the slot-2 gate answers, :159), but a
// seat-driven archmage keeps it, and for it a cast that worked ends the
// reply. (a) A summon that stands returns at once: one elemental, no coin,
// slot 3 and the shift still as they were, 300 magic. (b) A summon with
// nowhere to stand costs nothing with the setting on (fix D) and falls to
// slot 2: the coin (0) casts HEARTBURST (510), and with 510 left (>=
// TELEPORT's 10) the "then leave! :)" follow-up casts it again (215): two
// explosions, 1100 - 510 - 80 - 215 - 80 = 215 magic.
//
// RED (`self:special() and gf.on()` -> `self:special() and false` at
// :214): Expected equality of these values: coin1 ... world().rng_.state_
// "a summon that worked ends the reply". RED (the same at :271): Expected
// equality of these values: 2u bursts.size() Which is: 1 "HEARTBURST, then
// its follow-up".
TEST_F(SemArchmage, with_gameplay_fixes_a_seated_archmages_summon_that_stands_ends_its_reply)
{
    {
        world().gameplay_fixes = 1;
        const std::uint32_t coin1 = state_where_next_is(2, 1);
        living* a = arch(1, 200, 200, 10, 1100.0f);
        living* o = orc(2, 230, 200);
        ASSERT_TRUE(a && o);
        a->set_user(0);
        ASSERT_TRUE(og::sim::seat_driven(*a));
        a->set_shifter_down(1);
        world().rng_.state_ = coin1;
        hit(a, o);
        EXPECT_EQ(1u, live(Order::Living, FAMILY_FIREELEMENTAL).size()) << "the summon stands";
        EXPECT_EQ(coin1, world().rng_.state_) << "a summon that worked ends the reply";
        EXPECT_EQ(3, static_cast<int>(a->current_special())) << "slot 2 never tried";
        EXPECT_EQ(1, a->shifter_down());
        EXPECT_EQ(0, live_fx(FAMILY_EXPLOSION));
        EXPECT_EQ(0, live_fx(FAMILY_CHAIN));
        EXPECT_FLOAT_EQ(300.0f, a->stats()->magicpoints()) << "only the summon is paid";
    }
    {
        fresh_world();
        world().gameplay_fixes = 1;
        wall_around_160();
        const std::uint32_t coin0 = state_where_next_is(2, 0);
        living* a = arch(1, 160, 160, 10, 1100.0f);
        living* o = orc(2, 224, 160);
        ASSERT_TRUE(a && o);
        a->set_user(0);
        ASSERT_TRUE(og::sim::seat_driven(*a));
        a->set_shifter_down(1);
        world().rng_.state_ = coin0;
        hit(a, o);
        EXPECT_EQ(0u, live(Order::Living, FAMILY_FIREELEMENTAL).size()) << "no room";
        const auto bursts = live(Order::FX, FAMILY_EXPLOSION);
        ASSERT_EQ(2u, bursts.size()) << "HEARTBURST, then its follow-up";
        std::vector<float> dmg = {bursts[0]->damage(), bursts[1]->damage()};
        std::sort(dmg.begin(), dmg.end());
        EXPECT_FLOAT_EQ(215.0f, dmg[0]) << "the follow-up burst";
        EXPECT_FLOAT_EQ(510.0f, dmg[1]) << "the first burst";
        EXPECT_EQ(0, live_fx(FAMILY_CHAIN));
        EXPECT_FLOAT_EQ(215.0f, a->stats()->magicpoints());
        EXPECT_EQ(state_after(coin0, {2}), world().rng_.state_) << "the slot-2 coin";
    }
}

namespace {

struct Struck {
    int explosions = 0;
    int chains = 0;
    float mp = 0.0f;
    int ani_type = 0;
    int slot = 0;
    int shift = 0;
    std::uint32_t rng = 0;
    std::vector<float> burst_damage;
    std::vector<float> chain_damage;
};

}  // namespace

class SemArchmageClassicSlot2 : public SemArchmage {
protected:
    // The classic order: a level-4 archmage (slots 0..2, SUMMON locked),
    // healthy, struck by an orc `dx` px east of it, the coin pinned to
    // `coin`. With `seated` the setting is on and a seat drives the
    // archmage (the one way the classic order runs with Gameplay Fixes);
    // otherwise the setting is off.
    Struck struck(float mp, std::uint32_t coin, short dx = 30, bool seated = false)
    {
        fresh_world();
        Struck s;
        living* a = arch(1, 200, 200, 4, mp);
        living* o = orc(2, static_cast<short>(200 + dx), 200);
        EXPECT_TRUE(a && o);
        if (!(a && o))
            return s;
        if (seated) {
            world().gameplay_fixes = 1;
            a->set_user(0);
        }
        EXPECT_EQ(seated, og::sim::seat_driven(*a));
        o->stats()->set_max_hitpoints(900.0f);
        o->stats()->set_hitpoints(900.0f);
        const std::uint32_t seed = state_where_next_is(2, coin);
        world().rng_.state_ = seed;
        hit(a, o);
        for (walker* b : live(Order::FX, FAMILY_EXPLOSION))
            s.burst_damage.push_back(b->damage());
        for (walker* c : live(Order::FX, FAMILY_CHAIN))
            s.chain_damage.push_back(c->damage());
        s.explosions = static_cast<int>(s.burst_damage.size());
        s.chains = static_cast<int>(s.chain_damage.size());
        s.mp = a->stats()->magicpoints();
        s.ani_type = a->ani_type();
        s.slot = static_cast<int>(a->current_special());
        s.shift = a->shifter_down();
        s.rng = state_after(seed, {2}) == world().rng_.state_ ? 1u : 0u;
        return s;
    }
};

// :236 and :246 the chain arm (rand(2) != 0), setting off. The CHAIN takes
// 460 (half the 920 spare) and 80, and its answer is never read, as in the
// old game: the reply goes on to the HEARTBURST attempt (the shift released,
// slot 2), which the chain's 5 ticks of busy refuse. One chain, no
// explosion, 460 magic: classic never casts a second time after a chain.
//
// RED (`self:special() and gf.on()` -> `self:special()` at :246): Expected
// equality of these values: 0 off.explosions Which is: 1 "the HEARTBURST
// after the chain is refused".
TEST_F(SemArchmageClassicSlot2, classic_a_struck_archmages_chain_is_never_followed_by_a_second_cast)
{
    const Struck off = struck(1000.0f, 1);
    EXPECT_EQ(1, off.chains) << "one CHAIN";
    ASSERT_EQ(1u, off.chain_damage.size());
    EXPECT_FLOAT_EQ(460.0f, off.chain_damage[0]);
    EXPECT_EQ(0, off.explosions) << "the HEARTBURST after the chain is refused";
    EXPECT_FLOAT_EQ(460.0f, off.mp) << "the chain alone is paid";
    EXPECT_EQ(2, off.slot) << "the reply went on to slot 2's HEARTBURST";
    EXPECT_EQ(0, off.shift) << "the shift released for the HEARTBURST attempt";
    EXPECT_NE(ANI_TELE_OUT, off.ani_type);
    EXPECT_EQ(1u, off.rng) << "one draw: the coin";
}

// :246 and :255 the chain arms with the setting on, for a seat-driven
// archmage. With 1000 magic the CHAIN takes 460 and 80, and with 460 left
// (>= TELEPORT's 10) the follow-up casts slot 2 again with the shift
// released: a HEARTBURST of 190, not a teleport. With 90 magic the CHAIN
// takes 5 and 80, and with 5 left there is no follow-up.
//
// RED (`self:special() and gf.on()` -> `self:special() and false` at
// :246): Expected equality of these values: 1 rich.explosions Which is: 0
// "chain, then the follow-up heartburst". RED (`mp >= cost(1)` -> `mp >=
// 9999` at :255): the same line.
TEST_F(SemArchmageClassicSlot2, with_gameplay_fixes_a_seated_archmage_chains_and_then_heartbursts_when_it_can_still_pay_a_teleport)
{
    const Struck rich = struck(1000.0f, 1, 30, true);
    EXPECT_EQ(1, rich.chains) << "one CHAIN";
    EXPECT_EQ(1, rich.explosions) << "chain, then the follow-up heartburst";
    ASSERT_EQ(1u, rich.chain_damage.size());
    EXPECT_FLOAT_EQ(460.0f, rich.chain_damage[0]);
    ASSERT_EQ(1u, rich.burst_damage.size());
    EXPECT_FLOAT_EQ(190.0f, rich.burst_damage[0]);
    EXPECT_FLOAT_EQ(190.0f, rich.mp);
    EXPECT_NE(ANI_TELE_OUT, rich.ani_type) << "the follow-up is not a teleport";
    EXPECT_EQ(1u, rich.rng) << "one draw: the coin";

    const Struck poor = struck(90.0f, 1, 30, true);
    EXPECT_EQ(1, poor.chains) << "one CHAIN";
    EXPECT_EQ(0, poor.explosions) << "5 left: no follow-up";
    EXPECT_FLOAT_EQ(5.0f, poor.mp);
    EXPECT_NE(ANI_TELE_OUT, poor.ani_type);
}

// :262 and :271 the burst arms (rand(2) == 0), setting off. With 1000 magic
// HEARTBURST is cast once (460) and its answer is never read, so the reply
// ends: no follow-up, no CHAIN, no teleport, 460 magic. With the orc 150 px
// off (inside the response's 200 px, outside HEARTBURST's 88 at level 4) the
// burst fails and nothing is cast or spent (CHAIN would have reached it).
//
// RED (`self:special() and gf.on()` -> `self:special()` at :271): Expected
// equality of these values: 1 once.explosions Which is: 2 "one HEARTBURST
// cast".
TEST_F(SemArchmageClassicSlot2, classic_a_struck_archmage_heartbursts_once_and_a_failed_burst_casts_nothing)
{
    const Struck once = struck(1000.0f, 0);
    EXPECT_EQ(1, once.explosions) << "one HEARTBURST cast";
    EXPECT_EQ(0, once.chains);
    ASSERT_EQ(1u, once.burst_damage.size());
    EXPECT_FLOAT_EQ(460.0f, once.burst_damage[0]);
    EXPECT_FLOAT_EQ(460.0f, once.mp);
    EXPECT_NE(ANI_TELE_OUT, once.ani_type);
    EXPECT_EQ(1u, once.rng) << "the coin is the one draw";

    const Struck miss = struck(1000.0f, 0, 150);
    EXPECT_EQ(0, miss.explosions) << "burst fails: nothing";
    EXPECT_EQ(0, miss.chains) << "burst fails: nothing";
    EXPECT_FLOAT_EQ(1000.0f, miss.mp);
    EXPECT_EQ(1u, miss.rng) << "the coin is the one draw";
}

// :271 and :279 the burst arms with the setting on, for a seat-driven
// archmage. With 1000 magic HEARTBURST is cast twice (460, then 190), no
// CHAIN, no teleport. With 90 magic the burst takes 5 and 80, and with 5
// left there is no follow-up. With the orc 150 px off the burst fails and
// nothing is cast or spent.
//
// RED (`self:special() and gf.on()` -> `self:special() and false` at
// :271): Expected equality of these values: 2 twice.explosions Which is: 1
// "two heartburst casts".
TEST_F(SemArchmageClassicSlot2, with_gameplay_fixes_a_seated_archmage_heartbursts_twice_and_a_failed_burst_casts_nothing)
{
    const Struck twice = struck(1000.0f, 0, 30, true);
    EXPECT_EQ(2, twice.explosions) << "two heartburst casts";
    EXPECT_EQ(0, twice.chains);
    ASSERT_EQ(2u, twice.burst_damage.size());
    std::vector<float> dmg = twice.burst_damage;
    std::sort(dmg.begin(), dmg.end());
    EXPECT_FLOAT_EQ(190.0f, dmg[0]) << "the follow-up burst";
    EXPECT_FLOAT_EQ(460.0f, dmg[1]) << "the first burst";
    EXPECT_FLOAT_EQ(190.0f, twice.mp);
    EXPECT_NE(ANI_TELE_OUT, twice.ani_type);

    const Struck poor = struck(90.0f, 0, 30, true);
    EXPECT_EQ(1, poor.explosions) << "5 left: no follow-up";
    EXPECT_FLOAT_EQ(5.0f, poor.mp);

    const Struck miss = struck(1000.0f, 0, 150, true);
    EXPECT_EQ(0, miss.explosions) << "burst fails: nothing";
    EXPECT_EQ(0, miss.chains) << "burst fails: nothing";
    EXPECT_FLOAT_EQ(1000.0f, miss.mp);
    EXPECT_EQ(1u, miss.rng) << "the coin is the one draw";
}

// :236 the chain-fails arm (rand(2) != 0). A failed CHAIN LIGHTNING falls
// through to a HEARTBURST attempt: the shift is released and slot 2 is cast
// again. The one way to stage a failed chain is the current game's
// per-placed-NPC "specials disabled" flag (level files v10, npc_flags bit 0;
// walker::special refuses every cast before charging anything), so this is
// pinned as a current-game rule: the old game has no such flag. The other
// way a chain fails, og.summon answering nil (COULD NOT CREATE CHAIN), needs
// a failing entity factory and is unreachable here; the old game
// dereferences the null chain there.
//
// The fall-through is seen in the walker's state: the coin fell on the
// chain (shift down), and the archmage ends with the shift released and
// slot 2 selected, having cast nothing and spent nothing.
//
// RED (`self:set_shifter_down(0)` -> `self:set_shifter_down(1)` at
// living-17-archmage.lua:262): Expected equality of these values: 0
// a->shifter_down() Which is: 1.
TEST_F(SemArchmageClassicSlot2, classic_a_struck_archmage_whose_chain_fails_falls_through_to_a_heartburst_attempt)
{
    fresh_world();
    living* a = arch(1, 200, 200, 4, 1000.0f);
    living* o = orc(2, 230, 200);
    ASSERT_TRUE(a && o);
    o->stats()->set_max_hitpoints(900.0f);
    o->stats()->set_hitpoints(900.0f);
    a->set_specials_disabled(true);
    a->set_shifter_down(0);
    a->set_current_special(0);
    world().gameplay_fixes = 0;
    const std::uint32_t seed = state_where_next_is(2, 1);
    world().rng_.state_ = seed;
    hit(a, o);
    EXPECT_EQ(0, a->shifter_down()) << "chain fails: the shift is released for the heartburst";
    EXPECT_EQ(2, static_cast<int>(a->current_special())) << "chain fails: slot 2 again";
    EXPECT_EQ(0u, live(Order::FX, FAMILY_CHAIN).size()) << "chain fails: no CHAIN";
    EXPECT_EQ(0u, live(Order::FX, FAMILY_EXPLOSION).size()) << "the heartburst is refused too";
    EXPECT_FLOAT_EQ(1000.0f, a->stats()->magicpoints()) << "nothing is charged";
    EXPECT_EQ(state_after(seed, {2}), world().rng_.state_) << "one draw: the coin";
    EXPECT_FLOAT_EQ(900.0f, o->stats()->hitpoints()) << "the orc is untouched";
}

// ---------------------------------------------------------------------------
// The bot gates (:168, :190)
// ---------------------------------------------------------------------------

// :168 >3 foes arm. Four foes within 110 px: the gate says yes (the escape)
// and writes the plain TELEPORT over a coin that fell on Shift.
//
// RED (`mark and not busy` -> `true` at :177): Expected equality of these
// values: 0 a->shifter_down() Which is: 1.
TEST_F(SemArchmage, a_crowded_bot_archmage_flees_with_a_plain_teleport_never_a_marker)
{
    living* a = arch(1, 200, 200, 10, 2000.0f);
    ASSERT_NE(nullptr, a);
    for (short y : {short{200}, short{220}, short{240}, short{260}})
        ASSERT_NE(nullptr, add(FAMILY_SOLDIER, 2, 230, y, 1));
    a->set_shifter_down(1);
    EXPECT_TRUE(gate(a, 1, 1)) << "four foes: flee";
    EXPECT_EQ(0, a->shifter_down()) << "four foes: a plain TELEPORT";
}

namespace {

// Replaces one family's tuning for the scope, restoring the installed map.
class ScopedTuning {
public:
    ScopedTuning(Order order, int family, std::initializer_list<std::pair<const char*, std::int64_t>> set)
        : order_(order), family_(family)
    {
        if (const og::script::TuningMap* m = og::script::family_tuning(order, family))
            saved_ = *m;
        og::script::TuningMap next = saved_;
        for (const auto& [key, value] : set) {
            bool found = false;
            for (auto& p : next) {
                if (p.key == key) {
                    p.value.kind = og::script::TuningValue::Kind::Integer;
                    p.value.integer = value;
                    found = true;
                }
            }
            EXPECT_TRUE(found) << key;
        }
        og::script::set_family_tuning(order, family, std::move(next));
    }
    ~ScopedTuning() { og::script::set_family_tuning(order_, family_, saved_); }
    ScopedTuning(const ScopedTuning&) = delete;
    ScopedTuning& operator=(const ScopedTuning&) = delete;

private:
    Order order_;
    int family_;
    og::script::TuningMap saved_;
};

}  // namespace

// :190 coin chain, only burst target arm. With the core tuning the chain's
// radius always contains the burst's, so this arm needs a tuning whose chain
// is the shorter: chain base 0 and 0 per level gives 20 px at level 10, the
// burst 100. A foe 50 px off is a burst target only: the coin on CHAIN is
// swapped to HEARTBURST and the gate says yes.
//
// RED (the swap `set_shifter_down(0)` dropped at :206): Expected equality of
// these values: 0 a->shifter_down() Which is: 1.
TEST_F(SemArchmage, a_bot_archmage_whose_chain_coin_has_only_a_burst_target_swaps_to_heartburst)
{
    ScopedTuning t(Order::Living, FAMILY_ARCHMAGE,
                   {{"chain_radius_base", 0}, {"chain_radius_per_level", 0}});
    living* a = arch(1, 200, 200, 10, 2000.0f);
    ASSERT_NE(nullptr, orc(2, 250, 200));
    ASSERT_NE(nullptr, a);
    a->set_shifter_down(1);
    EXPECT_TRUE(gate(a, 2, 1)) << "the burst has a target";
    EXPECT_EQ(0, a->shifter_down()) << "the coin is swapped to HEARTBURST";

    a->set_shifter_down(1);
    EXPECT_TRUE(gate(a, 2, 0)) << "setting off: the classic answer";
    EXPECT_EQ(1, a->shifter_down()) << "setting off: the coin is left as it fell";
}

// Gameplay Fixes AN (#453). The slot-2 rule measures CHAIN LIGHTNING with the
// cast's own reach: 200 + Int/2 + 2L for an archmage with a character record
// (a hero the computer plays), 200 + 5L + 2L for a monster. A level-14 hero
// with 60 Intelligence reaches 200 + 30 + 28 = 258 px (the monster reach would
// be 298, its HEARTBURST reaches 108): a foe at 258 px keeps the chain coin
// and the cast lands; at 259 px the rule swaps to a HEARTBURST with no target
// and says no, and the cast agrees ("NO FOE IN RANGE"). A level-1 hero with
// 200 Intelligence reaches 302 px (the monster reach would be 207): a foe at
// 300 px turns a HEARTBURST coin to the chain, and the chain lands. With the
// setting off the rule is the classic yes and the coin stays as it fell.
//
// RED (the hero branch dropped, `if self:has_guy() then` -> `if false then`
// in ai_burst_or_chain): Expected equality of these values: k.yes Which is:
// false gate(a, 2, 1) Which is: true "level 14, Int 60, foe at 259"; and
// k.yes Which is: true ... Which is: false "level 1, Int 200, foe at 300".
TEST_F(SemArchmage, a_bot_heros_chain_rule_reaches_what_its_cast_reaches)
{
    struct Case { int level; short intelligence; short dx; short coin; bool yes; short arm;
                  const char* what; };
    for (const Case k : {Case{14, 60, 258, 1, true, 1, "level 14, Int 60, foe at 258"},
                         Case{14, 60, 259, 1, false, 0, "level 14, Int 60, foe at 259"},
                         Case{1, 200, 300, 0, true, 1, "level 1, Int 200, foe at 300"}}) {
        fresh_world();
        living* a = arch(1, 200, 200, k.level, 2000.0f);
        ASSERT_NE(nullptr, orc(2, static_cast<short>(200 + k.dx), 200));
        ASSERT_NE(nullptr, a);
        give_character(a, k.intelligence);
        a->set_shifter_down(k.coin);
        EXPECT_EQ(k.yes, gate(a, 2, 1)) << k.what;
        EXPECT_EQ(k.arm, a->shifter_down()) << k.what;
        a->stats()->set_special_cost(2, 0);
        const Cast c = cast(a, 2, 1);
        EXPECT_EQ(k.yes, c.ok) << k.what << " (" << c.reason << "): the rule and the cast agree";
        EXPECT_EQ(k.yes ? 1 : 0, live_fx(FAMILY_CHAIN)) << k.what;
        if (!k.yes) {
            EXPECT_EQ("NO FOE IN RANGE", c.reason) << k.what;
        }
    }

    fresh_world();
    living* a = arch(1, 200, 200, 14, 2000.0f);
    ASSERT_NE(nullptr, orc(2, 459, 200));
    ASSERT_NE(nullptr, a);
    give_character(a, 60);
    a->set_shifter_down(1);
    EXPECT_TRUE(gate(a, 2, 0)) << "setting off: the classic answer";
    EXPECT_EQ(1, a->shifter_down()) << "setting off: the coin is left as it fell";
}

// #455, the bot's slot-2 rule. With the setting on the chain arm counts only
// foes on the archmage's own floor (the bolt strikes first nobody else);
// HEARTBURST's arm stays floor-blind (the cast bursts each foe on its own
// floor). A level-10 monster archmage on floor 0 (HEARTBURST reaches 100,
// the chain 270) with the chain coin and its only foe upstairs: at 50 px,
// inside the burst, the coin is swapped to HEARTBURST and the rule says
// yes; at 150 px, outside it, the rule says no; the same foe moved to
// floor 0 keeps the chain coin. With the setting off the rule is the
// classic yes and the coin stays as it fell.
//
// RED (the floor filter dropped, `local in_chain = #G.on_floor(og.find_foes_
// in_range(...), self:floor())` -> `local _, in_chain = og.find_foes_in_range
// (...)`): Expected equality of these values: 0 a->shifter_down() Which is: 1
// "swapped to HEARTBURST"; Value of: gate(a, 2, 1) Actual: true Expected:
// false "150 px upstairs: no target for either arm".
TEST_F(SemArchmage, a_bot_archmages_chain_wants_a_foe_on_its_own_floor)
{
    world().set_floor_count(2);
    living* a = arch(1, 200, 200, 10, 2000.0f);
    living* up = orc(2, 250, 200);
    ASSERT_TRUE(a && up);
    up->set_floor(1);
    a->set_shifter_down(1);
    EXPECT_TRUE(gate(a, 2, 1)) << "50 px upstairs: a HEARTBURST target";
    EXPECT_EQ(0, a->shifter_down()) << "swapped to HEARTBURST";

    up->setxy(350, 200);
    a->set_shifter_down(1);
    EXPECT_FALSE(gate(a, 2, 1)) << "150 px upstairs: no target for either arm";

    up->set_floor(0);
    a->set_shifter_down(1);
    EXPECT_TRUE(gate(a, 2, 1)) << "150 px on its floor: the chain";
    EXPECT_EQ(1, a->shifter_down()) << "the chain coin kept";

    up->set_floor(1);
    a->set_shifter_down(1);
    EXPECT_TRUE(gate(a, 2, 0)) << "setting off: the classic answer";
    EXPECT_EQ(1, a->shifter_down()) << "setting off: the coin is left as it fell";
}

// ---------------------------------------------------------------------------
// TELEPORT and the marker (:281 .. :327)
// ---------------------------------------------------------------------------

// :281 mid teleport arm, and the shifter-0 arm's unread parts. An archmage
// already teleporting (out or in) refuses TELEPORT ("SPECIAL BUSY") free.
// One that is walking goes out: true, ANI_TELE_OUT, cycle 0, one TELEPORT
// sound, 10 magic. The old game: `if (ani_type == ANI_TELE_OUT || ani_type
// == ANI_TELE_IN) return 0;`.
//
// RED (the mid-teleport guard dropped at :281): Value of: c.ok Actual: true
// Expected: false "teleporting out". RED (set_cycle(0) -> set_cycle(1) at
// :287): 0 a->cycle() Which is: '\x1'. RED (the sound dropped at :285): 1
// sounds(SOUND_TELEPORT) Which is: 0.
TEST_F(SemArchmage, an_archmage_already_teleporting_cannot_teleport_again)
{
    living* a = arch(0, 200, 200, 8, 100.0f);
    ASSERT_NE(nullptr, a);
    for (char ani : {static_cast<char>(ANI_TELE_OUT), static_cast<char>(ANI_TELE_IN)}) {
        a->set_ani_type(ani);
        a->set_busy(0.0f);
        tw_->events.clear();
        const Cast c = cast(a, 1, 0);
        const char* what = ani == ANI_TELE_OUT ? "teleporting out" : "teleporting in";
        EXPECT_FALSE(c.ok) << what;
        EXPECT_EQ("SPECIAL BUSY", c.reason) << what;
        EXPECT_FLOAT_EQ(100.0f, a->stats()->magicpoints()) << what << ": free";
        EXPECT_EQ(ani, a->ani_type()) << what;
        EXPECT_EQ(0, sounds(SOUND_TELEPORT)) << what;
    }

    a->set_ani_type(ANI_WALK);
    a->set_cycle(5);
    tw_->events.clear();
    const Cast go = cast(a, 1, 0);
    EXPECT_TRUE(go.ok) << go.reason;
    EXPECT_EQ(ANI_TELE_OUT, a->ani_type());
    EXPECT_EQ(0, a->cycle());
    EXPECT_EQ(1, sounds(SOUND_TELEPORT));
    EXPECT_FLOAT_EQ(90.0f, a->stats()->magicpoints());
}

// :303 someone else's marker arm. B places a marker, then A does: B's marker
// stands (one live marker per owner), nobody hears "(Old Marker Removed)",
// and A's placement costs 8 busy ticks only.
//
// RED (`ob:owner() == self` -> `ob:owner() ~= nil` at :306): Expected
// equality of these values: 0 mb->dead() Which is: 1.
TEST_F(SemArchmage, a_new_marker_retires_only_its_own_casters_old_marker)
{
    living* b = arch(0, 400, 400, 8, 9000.0f);
    living* a = arch(0, 200, 200, 8, 9000.0f);
    ASSERT_TRUE(a && b);
    ASSERT_TRUE(cast(b, 1, 1).ok);
    const auto first = live(Order::FX, FAMILY_MARKER);
    ASSERT_EQ(1u, first.size());
    walker* mb = first[0];
    ASSERT_EQ(b, mb->owner());
    tw_->events.clear();
    ASSERT_TRUE(cast(a, 1, 1).ok);
    EXPECT_EQ(0, mb->dead()) << "someone else's marker stands";
    const auto markers = live(Order::FX, FAMILY_MARKER);
    ASSERT_EQ(2u, markers.size()) << "one live marker per owner";
    EXPECT_NE(markers[0]->owner(), markers[1]->owner());
    EXPECT_EQ(0, notifications("Old Marker Removed"));
    EXPECT_FLOAT_EQ(8.0f, a->busy()) << "a new marker only: 8 busy ticks";
}

// :310 and :331 enemy monster arm. A team-1 archmage with no character
// places a marker and then replaces it: the first is retired, one stands,
// and not a word is told ("Old Marker Removed", "Teleport Marker Placed",
// "(N Uses)" are for the player's side and heroes). The old game: `if
// (team_num == 0 || myguy) do_notify(...)` at both sites.
//
// RED (the :331 guard -> `if true`): Expected equality of these values: 0
// notifications("Marker") Which is: 2. RED (the :310 guard -> `if true`): 0
// notifications("Marker") Which is: 1.
TEST_F(SemArchmage, an_enemy_archmage_places_and_replaces_its_marker_in_silence)
{
    living* a = arch(1, 200, 200, 8, 9000.0f);
    ASSERT_NE(nullptr, a);
    ASSERT_TRUE(cast(a, 1, 1).ok);
    const auto first = live(Order::FX, FAMILY_MARKER);
    ASSERT_EQ(1u, first.size());
    walker* m1 = first[0];
    a->set_busy(0.0f);
    ASSERT_TRUE(cast(a, 1, 1).ok);
    EXPECT_EQ(1, m1->dead()) << "the first marker is retired";
    EXPECT_EQ(1u, live(Order::FX, FAMILY_MARKER).size());
    EXPECT_FLOAT_EQ(16.0f, a->busy()) << "the replacement: 8 and 8";
    EXPECT_EQ(0, notifications("Marker")) << "an enemy monster: silent";
    EXPECT_EQ(0, notifications("Uses)")) << "an enemy monster: silent";
    EXPECT_EQ(0, all_notifications());
}

// :325 monster arm. A guy-less archmage's marker holds level / 4 + 1 uses:
// 1, 2, 3, 3 at levels 1, 4, 8, 11 (the old game: `(stats->level / 4) + 1`).
//
// RED (`// 4 + 1` -> `// 4 + 2` at :328): Expected equality of these values:
// 1 markers[0]->lifetime() Which is: 2 "level 1".
TEST_F(SemArchmage, a_monster_archmages_marker_holds_a_quarter_of_its_level_plus_one_uses)
{
    for (const auto& [level, uses] : {std::pair<int, int>{1, 1}, std::pair<int, int>{4, 2},
                                      std::pair<int, int>{8, 3}, std::pair<int, int>{11, 3}}) {
        fresh_world();
        living* a = arch(1, 200, 200, level, 9000.0f);
        ASSERT_NE(nullptr, a);
        ASSERT_TRUE(cast(a, 1, 1).ok) << "level " << level;
        const auto markers = live(Order::FX, FAMILY_MARKER);
        ASSERT_EQ(1u, markers.size());
        EXPECT_EQ(uses, markers[0]->lifetime()) << "level " << level;
    }
}

// ---------------------------------------------------------------------------
// HEARTBURST / CHAIN LIGHTNING (:344 .. :421)
// ---------------------------------------------------------------------------

// :344 busy arm. A busy archmage refuses both arms free, with a foe 10 px
// off (well inside either radius): no explosion, no chain, busy unchanged.
//
// RED (the busy guard dropped at :344): Value of: c.ok Actual: true
// Expected: false "shift 0".
TEST_F(SemArchmage, a_busy_archmage_cannot_heartburst_or_chain)
{
    living* a = arch(1, 200, 200, 8, 1000.0f);
    ASSERT_NE(nullptr, orc(2, 210, 200));
    ASSERT_NE(nullptr, a);
    for (short shift : {short{0}, short{1}}) {
        a->set_busy(5.0f);
        const Cast c = cast(a, 2, shift);
        EXPECT_FALSE(c.ok) << "shift " << shift;
        EXPECT_EQ("SPECIAL BUSY", c.reason) << "shift " << shift;
        EXPECT_FLOAT_EQ(1000.0f, a->stats()->magicpoints()) << "shift " << shift;
        EXPECT_FLOAT_EQ(5.0f, a->busy()) << "shift " << shift;
    }
    EXPECT_EQ(0, live_fx(FAMILY_EXPLOSION));
    EXPECT_EQ(0, live_fx(FAMILY_CHAIN));
}

// :349 burst and monster-chain arms (the hero arm is the next test). At level 1
// HEARTBURST reaches 80 + 2 = 82 px (Manhattan, top-left corners): a foe at
// 82 px is burst, at 83 px it is "NO FOE IN RANGE" and costs nothing. A
// monster's CHAIN reaches 200 + 5 + 2 = 207 px: 207 is struck, 208 is not.
// The old game: `generic = 80` / `200+stats->level*5`, scanned at
// `generic+2*stats->level`.
//
// RED (burst_radius_base + 1 in the cast, `radius = t.burst_radius_base + 1`
// at :357): Expected equality of these values: k.hits Which is: false c.ok
// Which is: true "burst at 83".
TEST_F(SemArchmage, heartburst_reaches_80_plus_2l_and_a_monsters_chain_200_plus_7l)
{
    struct Case { short shift; short dx; bool hits; int family; const char* what; };
    for (const Case k : {Case{0, 82, true, FAMILY_EXPLOSION, "burst at 82"},
                         Case{0, 83, false, FAMILY_EXPLOSION, "burst at 83"},
                         Case{1, 207, true, FAMILY_CHAIN, "chain at 207"},
                         Case{1, 208, false, FAMILY_CHAIN, "chain at 208"}}) {
        fresh_world();
        living* a = arch(1, 200, 200, 1, 2000.0f);
        ASSERT_NE(nullptr, orc(2, static_cast<short>(200 + k.dx), 200));
        ASSERT_NE(nullptr, a);
        a->stats()->set_special_cost(2, 0);
        const Cast c = cast(a, 2, k.shift);
        EXPECT_EQ(k.hits, c.ok) << k.what;
        EXPECT_EQ(k.hits ? 1 : 0, live_fx(k.family)) << k.what;
        if (k.hits) {
            EXPECT_FLOAT_EQ(1400.0f, a->stats()->magicpoints()) << k.what << ": the 600 pool";
        } else {
            EXPECT_EQ("NO FOE IN RANGE", c.reason) << k.what;
            EXPECT_FLOAT_EQ(2000.0f, a->stats()->magicpoints()) << k.what << ": free";
        }
    }
}

// :349 hero-chain arm. A hero's CHAIN reaches 200 + Int/2 + 2L: level 1,
// Int 100, so 200 + 50 + 2 = 252 px. A foe at 252 px is struck (one chain,
// the 600 pool spent); at 253 px the cast is "NO FOE IN RANGE" and free.
// The level term of a monster's radius (5L) is not in a hero's. The old game
// (walker.cpp:3348): `generic = 200+myguy->intelligence/2`, scanned at
// `generic+2*stats->level`.
//
// RED (`self:g_intelligence() // 2` -> `self:g_intelligence() // 2 + 1` at
// :352): Expected equality of these values: k.hits Which is: false c.ok
// Which is: true "hero chain at 253 ()".
TEST_F(SemArchmage, a_heros_chain_reaches_200_plus_half_its_intelligence_plus_2l)
{
    struct Case { short dx; bool hits; const char* what; };
    for (const Case k : {Case{252, true, "hero chain at 252"},
                         Case{253, false, "hero chain at 253"}}) {
        fresh_world();
        living* a = arch(1, 200, 200, 1, 2000.0f);
        ASSERT_NE(nullptr, orc(2, static_cast<short>(200 + k.dx), 200));
        ASSERT_NE(nullptr, a);
        give_character(a, 100);
        a->stats()->set_special_cost(2, 0);
        const Cast c = cast(a, 2, 1);
        EXPECT_EQ(k.hits, c.ok) << k.what << " (" << c.reason << ")";
        EXPECT_EQ(k.hits ? 1 : 0, live_fx(FAMILY_CHAIN)) << k.what;
        if (k.hits) {
            EXPECT_FLOAT_EQ(1400.0f, a->stats()->magicpoints()) << k.what << ": the 600 pool";
        } else {
            EXPECT_EQ("NO FOE IN RANGE", c.reason) << k.what;
            EXPECT_FLOAT_EQ(2000.0f, a->stats()->magicpoints()) << k.what << ": free";
        }
    }
}

// :361 no-foe arm. Alone, both arms fail free: "NO FOE IN RANGE", magic
// and busy untouched, nothing made.
//
// RED (`foe_count == 0` -> `foe_count == -1` at :361): Expected equality of
// these values: "NO FOE IN RANGE" c.reason Which is: "SPECIAL SCRIPT ERROR"
// "shift 0" (og.div by zero foes).
TEST_F(SemArchmage, heartburst_or_chain_with_nobody_in_reach_fails_free)
{
    living* a = arch(1, 200, 200, 8, 5000.0f);
    ASSERT_NE(nullptr, a);
    for (short shift : {short{0}, short{1}}) {
        const Cast c = cast(a, 2, shift);
        EXPECT_FALSE(c.ok) << "shift " << shift;
        EXPECT_EQ("NO FOE IN RANGE", c.reason) << "shift " << shift;
        EXPECT_FLOAT_EQ(5000.0f, a->stats()->magicpoints()) << "shift " << shift;
        EXPECT_FLOAT_EQ(0.0f, a->busy()) << "shift " << shift;
    }
    EXPECT_EQ(0, live_fx(FAMILY_EXPLOSION));
    EXPECT_EQ(0, live_fx(FAMILY_CHAIN));
}

// :364 burst on N foes. Level 8, 1200 magic, the slot priced at 0: the pool
// is 600 (half the spare), split over three orcs at 10/20/30 px, one of them
// upstairs: three explosions of 200 each, each centred on its foe on that
// foe's floor; 600 spent, 5 busy ticks. (The old game: `generic /= howmany`,
// one explosion per foe found; it has no floors.)
//
// RED (`pool = og.div(pool, foe_count)` -> `og.div(pool, 1)` at :368):
// Expected equality of these values: 200 b->damage() Which is: 600.
TEST_F(SemArchmage, heartburst_splits_its_pool_evenly_into_one_explosion_on_each_foe_in_reach)
{
    world().set_floor_count(2);
    living* a = arch(1, 200, 200, 8, 1200.0f);
    living* o1 = orc(2, 210, 200);
    living* o2 = orc(2, 220, 200);
    living* o3 = orc(2, 230, 200);
    ASSERT_TRUE(a && o1 && o2 && o3);
    o2->set_floor(1);
    a->stats()->set_special_cost(2, 0);
    const Cast c = cast(a, 2, 0);
    ASSERT_TRUE(c.ok) << c.reason;
    const auto bursts = live(Order::FX, FAMILY_EXPLOSION);
    ASSERT_EQ(3u, bursts.size()) << "one explosion per foe";
    for (walker* b : bursts) {
        EXPECT_FLOAT_EQ(200.0f, b->damage()) << "600 / 3";
        const walker* on = nullptr;
        for (const walker* o : {static_cast<walker*>(o1), static_cast<walker*>(o2),
                                static_cast<walker*>(o3)})
            if (b->xpos() + b->sizex() / 2 == o->xpos() + o->sizex() / 2 &&
                b->ypos() + b->sizey() / 2 == o->ypos() + o->sizey() / 2)
                on = o;
        ASSERT_NE(nullptr, on) << "each explosion is centred on a foe";
        EXPECT_EQ(on->floor(), b->floor()) << "on that foe's floor";
    }
    EXPECT_FLOAT_EQ(600.0f, a->stats()->magicpoints());
    EXPECT_FLOAT_EQ(5.0f, a->busy());
}

// :369 / :399 the shot tallies. A hero's HEARTBURST on three foes counts
// three shots (total and scenario); its CHAIN LIGHTNING on the same three
// counts one. A guy-less archmage casts both with no tally to write (the
// cast succeeds and the hook raises nothing). The old game: `total_shots +=
// howmany` / `total_shots++`.
//
// RED (`+ foe_count` -> `+ 1` at :370): Expected equality of these values:
// 3 a->myguy->total_shots Which is: 1 "hero burst on 3". RED (`+ 1` -> `+ 2`
// at :402): 4 a->myguy->total_shots Which is: 5 "hero chain: one shot". RED
// (`if self:has_guy()` -> `if true` at :369): Value of: burst.ok Actual: false
// "monster burst: SPECIAL SCRIPT ERROR".
TEST_F(SemArchmage, a_heros_heartburst_counts_a_shot_per_foe_and_its_chain_counts_one)
{
    {
        living* a = arch(0, 200, 200, 8, 1800.0f);
        ASSERT_NE(nullptr, a);
        give_character(a, 220);
        for (short dx : {short{10}, short{20}, short{30}})
            ASSERT_NE(nullptr, orc(2, static_cast<short>(200 + dx), 200));
        a->stats()->set_special_cost(2, 0);
        ASSERT_TRUE(cast(a, 2, 0).ok);
        EXPECT_EQ(3, a->myguy->total_shots) << "hero burst on 3";
        EXPECT_EQ(3, a->myguy->scen_shots) << "hero burst on 3";
        a->set_busy(0.0f);
        ASSERT_TRUE(cast(a, 2, 1).ok);
        EXPECT_EQ(4, a->myguy->total_shots) << "hero chain: one shot";
        EXPECT_EQ(4, a->myguy->scen_shots) << "hero chain: one shot";
    }
    {
        fresh_world();
        living* a = arch(1, 200, 200, 8, 1800.0f);
        ASSERT_NE(nullptr, a);
        for (short dx : {short{10}, short{20}, short{30}})
            ASSERT_NE(nullptr, orc(2, static_cast<short>(200 + dx), 200));
        a->stats()->set_special_cost(2, 0);
        const Cast burst = cast(a, 2, 0);
        EXPECT_TRUE(burst.ok) << "monster burst: " << burst.reason;
        EXPECT_EQ(3, live_fx(FAMILY_EXPLOSION));
        a->set_busy(0.0f);
        const Cast chain = cast(a, 2, 1);
        EXPECT_TRUE(chain.ok) << "monster chain: " << chain.reason;
        EXPECT_EQ(1, live_fx(FAMILY_CHAIN));
        EXPECT_EQ(nullptr, a->myguy) << "no tally to write";
    }
}

// :419 same-floor arm's other half. A nearer foe upstairs is no first
// strike: the bolt takes the foe on the archmage's own floor, 150 px off.
//
// RED (`foe:floor() == self:floor()` -> `true` at :419): Expected equality
// of these values: far bolt->leader() Which is: (the upstairs orc).
TEST_F(SemArchmage, chain_lightning_strikes_first_the_nearest_foe_on_its_own_floor)
{
    world().set_floor_count(2);
    living* a = arch(1, 200, 200, 8, 1200.0f);
    living* near_up = orc(2, 220, 200);
    living* far = orc(2, 350, 200);
    ASSERT_TRUE(a && near_up && far);
    near_up->set_floor(1);
    ASSERT_TRUE(cast(a, 2, 1).ok);
    const auto bolts = live(Order::FX, FAMILY_CHAIN);
    ASSERT_EQ(1u, bolts.size());
    EXPECT_EQ(far, bolts[0]->leader()) << "the nearest foe on the archmage's floor";
}

// #455. CHAIN LIGHTNING with foes in its reach only on another floor fails
// free: the bolt can strike first only a foe on the archmage's own floor,
// so with none there the cast refuses "NO FOE IN RANGE" before it pays
// anything: no busy, no shot on the hero's record, no bolt, no magic, no
// draw. A hero archmage on floor 0 with an orc 20 px off upstairs. The
// control adds an orc on floor 0 at 150 px: the cast lands, its one bolt
// leashed to that orc, and the pool is spent.
//
// RED (the refusal dropped, `#G.on_floor(foes, self:floor()) == 0` -> `== -1`
// in burst_or_chain's chain arm): Value of: c.ok Actual: true Expected: false;
// Expected equality of these values: "NO FOE IN RANGE" c.reason Which is: "".
// (The old game has no floors, so there is nothing to port.)
TEST_F(SemArchmage, chain_lightning_with_foes_only_on_another_floor_fails_free)
{
    world().set_floor_count(2);
    living* a = arch(1, 200, 200, 8, 1200.0f);
    living* up = orc(2, 220, 200);
    ASSERT_TRUE(a && up);
    up->set_floor(1);
    give_character(a, 60);
    a->stats()->set_special_cost(2, 0);
    const std::uint32_t before = world().rng_.state_;
    const Cast c = cast(a, 2, 1);
    EXPECT_FALSE(c.ok);
    EXPECT_EQ("NO FOE IN RANGE", c.reason);
    EXPECT_FLOAT_EQ(1200.0f, a->stats()->magicpoints()) << "nothing spent";
    EXPECT_FLOAT_EQ(0.0f, a->busy()) << "no busy";
    EXPECT_EQ(0, live_fx(FAMILY_CHAIN)) << "no bolt";
    EXPECT_EQ(0, a->myguy->total_shots) << "no shot on the record";
    EXPECT_EQ(0, a->myguy->scen_shots);
    EXPECT_EQ(before, world().rng_.state_) << "nothing drawn";

    living* here = orc(2, 350, 200);
    ASSERT_NE(nullptr, here);
    const Cast landed = cast(a, 2, 1);
    ASSERT_TRUE(landed.ok) << landed.reason;
    const auto bolts = live(Order::FX, FAMILY_CHAIN);
    ASSERT_EQ(1u, bolts.size());
    EXPECT_EQ(here, bolts[0]->leader()) << "the foe on the archmage's floor";
    EXPECT_FLOAT_EQ(600.0f, a->stats()->magicpoints()) << "half the pool spent";
    EXPECT_EQ(1, a->myguy->total_shots);
}

// ---------------------------------------------------------------------------
// SUMMON IMAGE / SUMMON ELEMENTAL (:432 .. :600)
// ---------------------------------------------------------------------------

// :432 busy arm. A busy archmage summons nothing, with either shift: "SPECIAL
// BUSY", no new living, magic and busy unchanged.
//
// RED (the busy guard dropped at :432): Value of: c.ok Actual: true
// Expected: false "shift 0".
TEST_F(SemArchmage, a_busy_archmage_cannot_summon)
{
    living* a = arch(1, 200, 200, 8, 2000.0f);
    ASSERT_NE(nullptr, a);
    a->stats()->set_special_cost(3, 0);
    const int before = live_livings();
    for (short shift : {short{0}, short{1}}) {
        a->set_busy(5.0f);
        const Cast c = cast(a, 3, shift);
        EXPECT_FALSE(c.ok) << "shift " << shift;
        EXPECT_EQ("SPECIAL BUSY", c.reason) << "shift " << shift;
        EXPECT_EQ(before, live_livings()) << "shift " << shift;
        EXPECT_FLOAT_EQ(2000.0f, a->stats()->magicpoints()) << "shift " << shift;
        EXPECT_FLOAT_EQ(5.0f, a->busy()) << "shift " << shift;
    }
}

namespace {

struct Box { int x0, y0, x1, y1; };

// The pixel box of a body of `w` x `h` with its top-left at (x, y).
Box box_at(int x, int y, int w, int h) { return {x, y, x + w - 1, y + h - 1}; }

}  // namespace

// :459 placement arms. The elemental stands in the first passable of the
// eight neighbouring cells, the west column first (i outer, j inner): open
// ground puts it at the north-west cell (x - (w+1), y - (h+1)); with that
// cell walled, at the west cell (x - (w+1), y); with the whole west column
// walled, at the north cell (x, y - (h+1)). It is level (8+1)/2 = 4, on the
// caster's team, owned. The old game's walker::special, case 3: `for (i=-1;
// i <= 1; i++) for (j=-1; j <= 1; j++)`.
//
// RED (the i and j loops swapped at :457-458): Expected equality of these
// values: c.ex Which is: 191 e->xpos() Which is: 208 "NW walled: the west
// cell".
TEST_F(SemArchmage, a_summoned_elemental_stands_in_the_first_free_cell_from_the_west_column)
{
    walker* probe = world().add_ob(Order::Living, FAMILY_FIREELEMENTAL);
    ASSERT_NE(nullptr, probe);
    const int w = probe->sizex();
    const int h = probe->sizey();
    probe->set_dead(1);
    const int x = 208;
    const int y = 208;

    enum class Walls { None, NorthWest, WestColumn };
    struct Case { Walls walls; int ex; int ey; const char* what; };
    for (const Case c : {Case{Walls::None, x - (w + 1), y - (h + 1), "open: the north-west cell"},
                         Case{Walls::NorthWest, x - (w + 1), y, "NW walled: the west cell"},
                         Case{Walls::WestColumn, x, y - (h + 1), "west walled: the north cell"}}) {
        fresh_world();
        if (c.walls != Walls::None) {
            const Box nw = box_at(x - (w + 1), y - (h + 1), w, h);
            const Box sw = box_at(x - (w + 1), y + (h + 1), w, h);
            wall_box(nw.x0, nw.y0, nw.x1, c.walls == Walls::NorthWest ? nw.y1 : sw.y1);
        }
        living* a = arch(1, static_cast<short>(x), static_cast<short>(y), 8, 1800.0f);
        ASSERT_NE(nullptr, a);
        a->stats()->set_special_cost(3, 0);
        const Cast cs = cast(a, 3, 1);
        ASSERT_TRUE(cs.ok) << c.what << ": " << cs.reason;
        const auto els = live(Order::Living, FAMILY_FIREELEMENTAL);
        ASSERT_EQ(1u, els.size()) << c.what;
        walker* e = els[0];
        EXPECT_EQ(c.ex, e->xpos()) << c.what;
        EXPECT_EQ(c.ey, e->ypos()) << c.what;
        EXPECT_EQ(4, e->stats()->level()) << c.what;
        EXPECT_EQ(1, static_cast<int>(e->team_num())) << c.what;
        EXPECT_EQ(a, e->owner()) << c.what;
    }
}

// :577 / :600 both illusion arms. With a free cell the phantom is a level
// (8+2)/3 = 3 decoy on the caster's team, owned, named "Phantom", 0 of 1 hp,
// sharing the caster's foe, and the summon costs 15 busy ticks. With every
// neighbour walled it is "NO ROOM TO SUMMON": no new living, no busy, and
// the illusion slot is never charged. The old game: `newob->foe = foe;
// max_hitpoints = 1; hitpoints = 0;` ... `if (!generic) { newob->dead = 1;
// return 0; }` and `busy += 15`.
//
// RED (`if not placed` -> `if false` at :600): Value of: c.ok Actual: true
// Expected: false "no free cell". RED (max_hp 1 -> 2 at :589): 1.0f
// p->stats()->max_hitpoints() Which is: 2 "free cell: one max hp"; (foe ->
// nil at :593) o p->foe() Which is: NULL.
TEST_F(SemArchmage, a_summoned_image_shares_its_casters_foe_and_with_no_room_is_cancelled)
{
    ConstantRandom zero(0);
    ScopedSimRandom fixed_draws(&zero);
    {
        living* a = arch(1, 200, 200, 8, 800.0f);
        living* o = orc(2, 300, 300);
        ASSERT_TRUE(a && o);
        a->set_foe(o);
        a->stats()->set_special_cost(3, 0);
        const Cast c = cast(a, 3, 0);
        ASSERT_TRUE(c.ok) << c.reason;
        const auto mine = owned_by(a);
        ASSERT_EQ(1u, mine.size());
        walker* p = mine[0];
        EXPECT_EQ(std::string("Phantom"), std::string(p->stats()->name));
        EXPECT_EQ(1, static_cast<int>(p->team_num()));
        EXPECT_EQ(3, p->stats()->level());
        EXPECT_FLOAT_EQ(0.0f, p->stats()->hitpoints());
        EXPECT_FLOAT_EQ(1.0f, p->stats()->max_hitpoints()) << "free cell: one max hp";
        EXPECT_EQ(o, p->foe()) << "free cell: the caster's foe";
        EXPECT_FLOAT_EQ(15.0f, a->busy()) << "free cell: 15 busy ticks";
    }
    {
        fresh_world();
        wall_around_160();
        living* a = arch(1, 160, 160, 8, 2000.0f);
        ASSERT_NE(nullptr, a);
        const int before = live_livings();
        const Cast c = cast(a, 3, 0);
        EXPECT_FALSE(c.ok) << "no free cell";
        EXPECT_EQ("NO ROOM TO SUMMON", c.reason);
        EXPECT_EQ(before, live_livings()) << "no new living";
        EXPECT_FLOAT_EQ(0.0f, a->busy());
        EXPECT_FLOAT_EQ(2000.0f, a->stats()->magicpoints());
    }
}

// ---------------------------------------------------------------------------
// MIND CONTROL (:611 .. :680)
// ---------------------------------------------------------------------------

// :611 / :620 busy and no-foe arms. A busy archmage with a level-1 orc 6 px
// off refuses ("SPECIAL BUSY"): the orc keeps its team, nothing is spent, no
// rest starts. Alone it is "NO FOE IN RANGE", free, no rest. The old game:
// `if (busy > 0) return 0;` ... `if (howmany < 1) return 0;`.
//
// RED (`foe_count < 1` -> `foe_count < 0` at :620): Expected equality of
// these values: "NO FOE IN RANGE" alone.reason Which is: "NO VALID TARGET".
TEST_F(SemArchmage, mind_control_refuses_a_busy_archmage_and_one_with_nobody_in_reach_free)
{
    living* a = arch(1, 200, 200, 10, 1000.0f);
    living* o = orc(2, 206, 200);
    ASSERT_TRUE(a && o);
    a->set_busy(5.0f);
    const Cast busy = cast(a, 4, 0);
    EXPECT_FALSE(busy.ok) << "busy";
    EXPECT_EQ("SPECIAL BUSY", busy.reason);
    EXPECT_EQ(2, static_cast<int>(o->team_num())) << "busy: the orc keeps its team";
    EXPECT_EQ(255, static_cast<int>(o->real_team_num()));
    EXPECT_FLOAT_EQ(1000.0f, a->stats()->magicpoints());
    EXPECT_EQ(0, a->special_rest_left());

    fresh_world();
    living* lone = arch(1, 200, 200, 10, 1000.0f);
    ASSERT_NE(nullptr, lone);
    const Cast alone = cast(lone, 4, 0);
    EXPECT_FALSE(alone.ok) << "nobody in reach";
    EXPECT_EQ("NO FOE IN RANGE", alone.reason);
    EXPECT_FLOAT_EQ(1000.0f, lone->stats()->magicpoints());
    EXPECT_EQ(0, lone->special_rest_left());
}

namespace {

int on_team(const std::vector<walker*>& obs, int team)
{
    int n = 0;
    for (const walker* o : obs)
        if (static_cast<int>(o->team_num()) == team)
            n++;
    return n;
}

}  // namespace

// :628 / :673 the budget. Three level-1 orcs in reach, every domination roll
// a success (rand(20) == 1). With 150 magic (the price, spare 0) exactly one
// is taken, the other two keep team 2 and their never-charmed 255, and the
// cast costs the price only: 0 left. With 160 (spare 10) two are taken, on
// both values of the setting (the old game takes the same men): with the
// setting off the cast costs its price alone, 10 left, as the old game's sap
// lowers only a local; with it on the second man costs the 10 (fix AL): 0
// left. With 1000 and a single orc in reach the cast costs the 150 alone:
// 850, on both values.
//
// RED (`budget < 10` -> `budget < 0` at :628): Expected equality of these
// values: c.taken Which is: 1 on_team(orcs, 1) Which is: 2 "spare 0". RED
// (every man charged, `controlled >= 1` / `controlled * 10` at :673-674):
// 850.0f a->stats()->magicpoints() Which is: 840 "one controlled: the price
// only".
TEST_F(SemArchmage, mind_control_takes_one_foe_on_the_price_and_one_more_per_10_spare)
{
    ConstantRandom one(1);
    ScopedSimRandom fixed_draws(&one);
    struct Case { short fixes; float mp; int taken; float left; const char* what; };
    for (const Case c : {Case{0, 150.0f, 1, 0.0f, "setting off, spare 0"},
                         Case{0, 160.0f, 2, 10.0f, "setting off, spare 10"},
                         Case{1, 150.0f, 1, 0.0f, "setting on, spare 0"},
                         Case{1, 160.0f, 2, 0.0f, "setting on, spare 10"}}) {
        fresh_world();
        world().gameplay_fixes = c.fixes;
        living* a = arch(1, 200, 200, 10, c.mp);
        ASSERT_NE(nullptr, a);
        std::vector<walker*> orcs;
        for (short dx : {short{6}, short{10}, short{14}}) {
            living* o = orc(2, static_cast<short>(200 + dx), 200);
            ASSERT_NE(nullptr, o);
            orcs.push_back(o);
        }
        const Cast cs = cast(a, 4, 0);
        ASSERT_TRUE(cs.ok) << c.what << ": " << cs.reason;
        EXPECT_EQ(c.taken, on_team(orcs, 1)) << c.what;
        EXPECT_EQ(3 - c.taken, on_team(orcs, 2)) << c.what;
        int untouched = 0;
        for (const walker* o : orcs)
            if (o->team_num() == 2 && o->real_team_num() == 255)
                untouched++;
        EXPECT_EQ(3 - c.taken, untouched) << c.what << ": the rest are left alone";
        EXPECT_FLOAT_EQ(c.left, a->stats()->magicpoints()) << c.what;
    }

    for (short fixes : {short{0}, short{1}}) {
        fresh_world();
        world().gameplay_fixes = fixes;
        living* a = arch(1, 200, 200, 10, 1000.0f);
        living* o = orc(2, 206, 200);
        ASSERT_TRUE(a && o);
        ASSERT_TRUE(cast(a, 4, 0).ok);
        EXPECT_EQ(1, static_cast<int>(o->team_num())) << "setting " << fixes;
        EXPECT_FLOAT_EQ(850.0f, a->stats()->magicpoints())
            << "setting " << fixes << ", one controlled: the price only";
    }
}

// Gameplay Fixes AL (#485). Five level-1 orcs in reach of a level-10
// archmage with 400 magic, every domination roll a success: the budget (the
// 250 spare plus 10) takes all five on both values of the setting. With the
// setting off the cast costs its 150 alone, as the old game's does (its sap
// loop lowers only a local): 250 left. With it on each man past the first
// costs 10 more, 40 in all (never more than the 250 spare): 210 left.
//
// RED (the setting dropped, `local extra = gf.on() and controlled > 1` ->
// `local extra = controlled > 1`): Expected equality of these values: fixes
// != 0 ? 210.0f : 250.0f Which is: 250 a->stats()->magicpoints() Which is:
// 210 (and the loop above: c.left Which is: 10 ... Which is: 0). RED (the
// setting inverted, `not gf.on()`): the same line, 250 / 210 and 210 / 250.
TEST_F(SemArchmage, mind_control_charges_ten_a_man_past_the_first_only_with_the_fixes_on)
{
    ConstantRandom one(1);
    ScopedSimRandom fixed_draws(&one);
    for (short fixes : {short{0}, short{1}}) {
        fresh_world();
        world().gameplay_fixes = fixes;
        living* a = arch(1, 200, 200, 10, 400.0f);
        ASSERT_NE(nullptr, a);
        std::vector<walker*> orcs;
        for (short dx : {short{6}, short{10}, short{14}, short{18}, short{22}}) {
            living* o = orc(2, static_cast<short>(200 + dx), 200);
            ASSERT_NE(nullptr, o);
            orcs.push_back(o);
        }
        const Cast c = cast(a, 4, 0);
        ASSERT_TRUE(c.ok) << "setting " << fixes << ": " << c.reason;
        EXPECT_EQ(5, on_team(orcs, 1)) << "setting " << fixes << ": all five taken";
        EXPECT_FLOAT_EQ(fixes != 0 ? 210.0f : 250.0f, a->stats()->magicpoints())
            << "setting " << fixes;
    }
}

// :633 already-charmed and generator arms, on both values of the setting. In
// reach of a team-2 archmage are only a soldier already charmed (real team 1),
// a tent (a generator, never-charmed) and a soldier with 11 ticks of charm
// running: each is skipped before the roll, so the cast is "NO VALID TARGET"
// and free, nothing is drawn, and every team stays. The old game:
// `real_team_num == 255 && query_order() == ORDER_LIVING && ... &&
// charm_left <= 10`.
//
// RED (the order test dropped from gameplay_fixes.lua charmable, :41): Value
// of: c.ok Actual: true Expected: false "setting 0"; the same for
// `charm_left() <= 10` -> `<= 11` at :633.
TEST_F(SemArchmage, mind_control_skips_the_charmed_and_generators_without_spending)
{
    for (short fixes : {short{0}, short{1}}) {
        fresh_world();
        world().gameplay_fixes = fixes;
        living* a = arch(2, 200, 200, 10, 1000.0f);
        living* charmed = add(FAMILY_SOLDIER, 0, 220, 200, 1);
        walker* tent = world().add_ob(Order::Generator, FAMILY_TENT);
        living* still = add(FAMILY_SOLDIER, 0, 200, 260, 1);
        ASSERT_TRUE(a && charmed && tent && still);
        charmed->set_real_team_num(1);
        tent->set_team_num(0);
        tent->set_real_team_num(255);
        tent->setxy(200, 240);
        still->set_charm_left(11);
        const std::uint32_t seed = 4242u;
        world().rng_.state_ = seed;
        const Cast c = cast(a, 4, 0);
        EXPECT_FALSE(c.ok) << "setting " << fixes;
        EXPECT_EQ("NO VALID TARGET", c.reason) << "setting " << fixes;
        EXPECT_FLOAT_EQ(1000.0f, a->stats()->magicpoints()) << "setting " << fixes;
        EXPECT_EQ(seed, world().rng_.state_) << "setting " << fixes << ": no roll";
        EXPECT_EQ(0, static_cast<int>(charmed->team_num()));
        EXPECT_EQ(1, static_cast<int>(charmed->real_team_num()));
        EXPECT_EQ(0, static_cast<int>(tent->team_num())) << "the generator is skipped";
        EXPECT_EQ(255, static_cast<int>(tent->real_team_num()));
        EXPECT_EQ(0, static_cast<int>(still->team_num())) << "charm running: skipped";
        EXPECT_EQ(11, still->charm_left());
    }
}

// :640 rand(20) == 0 arm. An equal-level foe (edge 0) that rolls 0 on the
// domination draw resists: it goes berserk onto the rand(8) team (5), its
// real team (2) remembered, for charm_duration(0) = 25 ticks, and the draws
// are exactly next(20) and next(8) (next(0) for the duration draws nothing).
// The old game: `if (generic < 0 || (!random(20)))` ... `colour =
// random(8)` ... `25 + random(generic*20)`.
//
// RED (`og.rand(20) == 0` -> `og.rand(20) == 99` at :643): Expected equality
// of these values: 5 o->team_num() Which is: 1 "setting 0".
TEST_F(SemArchmage, an_equal_level_foe_that_rolls_zero_resists_mind_control_and_goes_berserk)
{
    const std::uint32_t seed = state_where_draws_are({20, 8}, {0, 5});
    for (short fixes : {short{0}, short{1}}) {
        fresh_world();
        world().gameplay_fixes = fixes;
        living* a = arch(1, 200, 200, 10, 9000.0f);
        living* o = orc(2, 220, 200, 10);
        ASSERT_TRUE(a && o);
        world().rng_.state_ = seed;
        ASSERT_TRUE(cast(a, 4, 0).ok) << "setting " << fixes;
        EXPECT_EQ(5, static_cast<int>(o->team_num())) << "setting " << fixes;
        EXPECT_EQ(2, static_cast<int>(o->real_team_num())) << "setting " << fixes;
        EXPECT_EQ(25, o->charm_left()) << "setting " << fixes;
        EXPECT_EQ(state_after(seed, {20, 8}), world().rng_.state_) << "setting " << fixes;
    }
}
