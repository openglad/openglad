/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The cleric bot's shift with the Gameplay Fixes setting.
//
// In the classic game a bot cleric that rolled RAISE UNDEAD or RAISE GHOST
// tosses a coin (living::check_special) to pick the raise or its alternate,
// TURN UNDEAD, so half its turns go off at a ring with nothing undead in it
// (20 MP, the sound, nobody turned). With the setting on the coin is still
// drawn, and the cleric's gate then picks on purpose: TURN UNDEAD only with
// an undead foe it could turn inside its reach (walker:undead_foes_in_range,
// the same question the cast asks), else the raise. The gate's answer stays
// "yes" for those slots on both values.
//
// The bot gates (test_bot_gates_casters.cpp) then ask each slot for its
// payoff with the setting on: a raise wants a corpse the raise can find and
// a living foe within 130 px, RESURRECT a friendly corpse, the mace a foe
// within its orbit. The cases here that expect a raise or RESURRECT stage
// that corpse (add_corpse) beside the cleric.
//
// GameplayFixesClericRaise, below: where RAISE UNDEAD, RAISE GHOST and
// RESURRECT stand the body (issue #390, Gameplay Fixes T). The classic casts
// probe the nearest stain's own 16 x 16 box at its corner, so a small victim
// that died against a wall, in a one-tile corridor or under the cleric is
// refused CORPSE IS BLOCKED. With the setting on the body that will rise is
// probed (its own box and flags, the caster blocking it like any body),
// nudged up to 16 px from the corner, over every stain the finder answers,
// nearest first (packs/core/lib/corpses.lua).

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

living* add_living(TestGameWorld& tw, int family, unsigned char team, int x,
                   int y, short level)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(static_cast<short>(x), static_cast<short>(y));
    w->stats()->set_level(level);
    return dynamic_cast<living*>(w);
}

// A level-4 bot cleric at (100, 100): the classic reach is 4 * 4 = 16 px,
// the reach with the setting on is 60 px.
living* add_cleric(TestGameWorld& tw)
{
    living* cleric = add_living(tw, FAMILY_CLERIC, 1, 100, 100, 4);
    if (cleric != nullptr) {
        cleric->set_act_type(ACT_RANDOM);
        cleric->stats()->set_max_magicpoints(500.0f);
        cleric->stats()->set_magicpoints(500.0f);
    }
    return cleric;
}

// A bloodstain of `team` 6 px below the cleric's corner: the corpse
// find_nearest_blood answers (it never answers beyond about 28 px of
// centre), inside every raise's reach and RESURRECT's.
walker* add_corpse(TestGameWorld& tw, unsigned char team)
{
    walker* s = tw.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (s == nullptr)
        return nullptr;
    s->set_team_num(team);
    s->setxy(100, 106);
    return s;
}

// The shift the cleric's gate leaves for `slot`, starting from `preset`
// (the coin's side), and the gate's answer.
struct GateOutcome {
    bool answer = false;
    int shift = -1;
};

GateOutcome gate(living* cleric, int slot, short preset)
{
    const FamilyDescriptor& fd = *get_family_descriptor(FAMILY_CLERIC);
    cleric->set_current_special(static_cast<char>(slot));
    cleric->set_shifter_down(preset);
    GateOutcome out;
    out.answer = og::test::check_special_ai(fd, cleric);
    out.shift = static_cast<int>(cleric->shifter_down());
    return out;
}

// The first world-RNG state from which the next draw of next(bound) is
// `want`: pins the bot's coin without a spy (the sim calls the LCG inline).
std::uint32_t state_where_next_is(std::uint32_t bound, std::uint32_t want)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        if (probe.next(bound) == want)
            return seed;
    }
}

}  // namespace

// A skeleton 50 px away: outside the classic reach (16), inside the fixed
// one (60). The classic gate keeps whatever side the coin landed on; with the
// setting on the cleric turns, both raise slots, whatever the coin said.
// RED on the classic gate: with the setting on the shift stays 0.
TEST(GameplayFixesClericBot, turns_with_an_undead_foe_in_reach)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        living* skeleton = add_living(tw, FAMILY_SKELETON, 2, 150, 100, 1);
        ASSERT_NE(nullptr, skeleton);
        ASSERT_EQ(50, static_cast<int>(cleric->distance_to_ob(skeleton)));

        for (const int slot : {2, 3}) {
            const GateOutcome g = gate(cleric, slot, 0);
            EXPECT_TRUE(g.answer) << "slot " << slot << ", setting " << fixes;
            EXPECT_EQ(fixes != 0 ? 1 : 0, g.shift)
                << "slot " << slot << ": the coin said raise; setting "
                << fixes;
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Only living foes in reach (an orc 50 px away, a generator 10 px away), or
// an undead that cannot be turned (a skeleton under an invulnerability
// potion): there is nothing to turn, so with the setting on the cleric
// raises even when the coin said turn. The classic gate keeps the coin.
// RED on the classic gate: with the setting on the shift stays 1.
TEST(GameplayFixesClericBot, raises_with_nothing_to_turn_in_reach)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        ASSERT_NE(nullptr, add_living(tw, FAMILY_ORC, 2, 150, 100, 1));
        ASSERT_NE(nullptr, add_corpse(tw, 2));  // the raise's corpse
        walker* tower = tw.world().add_ob(Order::Generator, FAMILY_TOWER);
        ASSERT_NE(nullptr, tower);
        tower->set_team_num(2);
        tower->setxy(110, 100);
        living* warded = add_living(tw, FAMILY_SKELETON, 2, 100, 110, 1);
        ASSERT_NE(nullptr, warded);
        warded->set_invulnerable_left(50);

        for (const int slot : {2, 3}) {
            const GateOutcome g = gate(cleric, slot, 1);
            EXPECT_TRUE(g.answer) << "slot " << slot << ", setting " << fixes;
            EXPECT_EQ(fixes != 0 ? 0 : 1, g.shift)
                << "slot " << slot << ": the coin said turn; setting "
                << fixes;
        }

        // Once the ward is gone the skeleton can be turned, and is chosen.
        warded->set_invulnerable_left(0);
        const GateOutcome g = gate(cleric, 2, 0);
        EXPECT_EQ(fixes != 0 ? 1 : 0, g.shift)
            << "the unwarded skeleton, setting " << fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Slot 4 (RESURRECT) has no alternate: the turn arm never touches its
// shift. Slot 1 with no friend in reach and the pool full maces on both
// values: the classic rule at half mana, and with the setting on because the
// skeleton stands 30 px off, inside the mace's 48 px orbit. A friendly
// corpse beside the cleric is RESURRECT's payoff with the setting on.
// RED under the mutation "the fixed arm runs for every slot": slot 4's
// shift flips to 1 with the skeleton in reach.
TEST(GameplayFixesClericBot, leaves_the_resurrect_shift_and_maces_a_foe_in_orbit)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        ASSERT_NE(nullptr, add_living(tw, FAMILY_SKELETON, 2, 130, 100, 1));
        ASSERT_NE(nullptr, add_corpse(tw, 1));  // a fallen friend

        const GateOutcome resurrect = gate(cleric, 4, 0);
        EXPECT_TRUE(resurrect.answer);
        EXPECT_EQ(0, resurrect.shift) << "slot 4, setting " << fixes;

        // No friend in reach, full mana: the mace.
        const GateOutcome mace = gate(cleric, 1, 0);
        EXPECT_TRUE(mace.answer);
        EXPECT_EQ(1, mace.shift) << "slot 1 with no friend, setting " << fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Through living::check_special, the bot's own door: the coin
// (rng_.next(2)) is drawn on both values, so the world's stream is where the
// classic game leaves it, and with the setting on the gate's choice
// overrides the coin afterwards.
// RED on the classic gate: with the setting on the coin's 0 stays.
TEST(GameplayFixesClericBot, the_coin_is_drawn_and_then_overridden)
{
    og::test::ScopedHookFailureGuard guard;
    const std::uint32_t start = state_where_next_is(2, 0);
    og::sim::SimRandom one_coin(start);
    (void)one_coin.next(2);

    for (const short fixes : {short{0}, short{1}}) {
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        ASSERT_NE(nullptr, add_living(tw, FAMILY_SKELETON, 2, 150, 100, 1));
        cleric->set_current_special(2);
        cleric->set_shifter_down(1);
        tw.world().rng_.state_ = start;
        EXPECT_TRUE(cleric->check_special()) << "setting " << fixes;
        EXPECT_EQ(one_coin.state_, tw.world().rng_.state_)
            << "one coin and nothing else, setting " << fixes;
        EXPECT_EQ(fixes != 0 ? 1 : 0, static_cast<int>(cleric->shifter_down()))
            << "the coin said raise; setting " << fixes;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A cleric whose hero has less than the 60 INT a TURN UNDEAD needs: the cast
// refuses it ("60 INT REQUIRED", 5 busy), so with the setting on the gate
// asks the same question and raises instead, even with a skeleton in reach
// and the coin on turn. With 60 INT it turns. The classic gate keeps the coin
// on both INT values.
// RED on the gate without the INT question: with the setting on and 40 INT
// the shift stays 1.
TEST(GameplayFixesClericBot, raises_when_its_hero_lacks_the_int_to_turn)
{
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        for (const short intelligence : {short{40}, short{60}}) {
            TestGameWorld tw;
            tw.world().gameplay_fixes = fixes;
            living* cleric = add_cleric(tw);
            ASSERT_NE(nullptr, cleric);
            cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
            ASSERT_NE(nullptr, cleric->myguy);
            cleric->myguy->intelligence = intelligence;
            ASSERT_NE(nullptr, add_living(tw, FAMILY_SKELETON, 2, 150, 100, 1));
            ASSERT_NE(nullptr, add_corpse(tw, 2));  // the raise's corpse

            const int want = (fixes != 0 && intelligence < 60) ? 0 : 1;
            for (const int slot : {2, 3}) {
                const GateOutcome g = gate(cleric, slot, 1);
                EXPECT_TRUE(g.answer)
                    << "slot " << slot << ", INT " << intelligence
                    << ", setting " << fixes;
                EXPECT_EQ(want, g.shift)
                    << "slot " << slot << ": the coin said turn; INT "
                    << intelligence << ", setting " << fixes;
            }
        }
    }

    // The cast asks the same question and still refuses.
    {
        TestGameWorld tw;
        tw.world().gameplay_fixes = 1;
        living* cleric = add_cleric(tw);
        ASSERT_NE(nullptr, cleric);
        cleric->set_owned_myguy(std::make_unique<guy>(FAMILY_CLERIC));
        ASSERT_NE(nullptr, cleric->myguy);
        cleric->myguy->intelligence = 40;
        ASSERT_NE(nullptr, add_living(tw, FAMILY_SKELETON, 2, 150, 100, 1));

        const FamilyDescriptor& desc = *get_family_descriptor(FAMILY_CLERIC);
        cleric->set_current_special(2);
        cleric->set_shifter_down(1);
        cleric->set_busy(0);
        const auto turn = og::script::hooks::do_special(&desc, cleric);
        ASSERT_TRUE(turn.has_value());
        EXPECT_FALSE(turn->succeeded());
        EXPECT_EQ(std::string("60 INT REQUIRED"), turn->reason());
        EXPECT_FLOAT_EQ(5.0f, cleric->busy());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// GameplayFixesClericRaise: where the body can stand (#390, fix T)
// ---------------------------------------------------------------------------

namespace {

// The rig: TestGameWorld's 40 x 60 grass map, walls painted by hand, a
// level-10 cleric of team 0 with 600 MP, and victims that really die (hp 0,
// then generate_bloodspot, the death path's own stain).

void wall_column(GameWorld& w, int col)
{
    for (int row = 0; row < w.grid.h; ++row)
        w.grid.data[static_cast<std::size_t>(col + w.grid.w * row)] = PIX_H_WALL1;
}

void wall_cell(GameWorld& w, int col, int row)
{
    w.grid.data[static_cast<std::size_t>(col + w.grid.w * row)] = PIX_H_WALL1;
}

living* raiser(TestGameWorld& tw, int x, int y)
{
    living* c = add_living(tw, FAMILY_CLERIC, 0, x, y, 10);
    if (c != nullptr) {
        c->stats()->set_max_magicpoints(600.0f);
        c->stats()->set_magicpoints(600.0f);
    }
    return c;
}

// A `family` of `team` killed with its top-left at (x, y): the stain it
// leaves (16 x 16, at the victim's corner, the victim's team and family).
walker* fallen(TestGameWorld& tw, int family, unsigned char team, int x, int y)
{
    living* v = add_living(tw, family, team, x, y, 1);
    if (v == nullptr)
        return nullptr;
    v->stats()->set_hitpoints(0.0f);
    v->generate_bloodspot();
    walker* stain = nullptr;
    for (auto& u : tw.world().fxlist)
        if (u && u->query_order() == Order::Treasure &&
            u->family() == FAMILY_STAIN && !u->dead())
            stain = u.get();
    return stain;
}

// How many world draws lie between two states of the stream (-1: more than
// 64, or none of them).
int draws_between(std::uint32_t from, std::uint32_t to)
{
    og::sim::SimRandom r(from);
    for (int n = 0; n <= 64; ++n) {
        if (r.state_ == to)
            return n;
        (void)r.next(2);
    }
    return -1;
}

// The live livings of `family`, other than `except`.
std::vector<walker*> live_of(GameWorld& w, int family, const walker* except)
{
    std::vector<walker*> out;
    for (auto& u : w.oblist)
        if (u && u.get() != except && u->query_order() == Order::Living &&
            u->family() == family && !u->dead())
            out.push_back(u.get());
    return out;
}

struct Raise {
    bool ok = false;
    std::string reason;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    int draws = -1;
    float mp_spent = 0.0f;
    std::size_t events = 0;
};

// The cleric's special in `slot` (unshifted), as the player casts it.
Raise cast_raise(TestGameWorld& tw, living* cleric, int slot)
{
    cleric->set_current_special(static_cast<char>(slot));
    cleric->set_shifter_down(0);
    cleric->set_busy(0);
    tw.events.clear();
    const float mp = cleric->stats()->magicpoints();
    const std::uint32_t before = tw.world().rng_.state_;
    Raise r;
    r.ok = cleric->special(&r.why, &r.reason);
    r.draws = draws_between(before, tw.world().rng_.state_);
    r.mp_spent = mp - cleric->stats()->magicpoints();
    r.events = tw.events.size();
    return r;
}

// The body slot `slot` raises from a foe's stain (RESURRECT: a ghost).
int risen_family(int slot)
{
    return slot == 2 ? FAMILY_SKELETON : FAMILY_GHOST;
}

}  // namespace

// The reporter's case: an elf (10 x 10) dies with its right side on a wall
// (column 15, x 240), the cleric beside it at (213, 200). Its stain's 16 x 16
// box reaches 6 px into the wall, so the classic probe refuses CORPSE IS
// BLOCKED on every slot. With the setting on:
//  - RAISE UNDEAD: the 15 x 13 skeleton fits nowhere on the stain's row (the
//    wall on the right, the cleric's box, x 213..228, on the left: x <= 225
//    and x >= 229 at once), nor straight up or down (x 230 is in the wall
//    from x + 15 > 240); the first fit in the ring is (-12, +12), (218, 212),
//    just under the cleric's feet (its box ends at y 211);
//  - RAISE GHOST: the ghost is ethereal (the grid never stops it) and clear
//    of the cleric at the corner: (230, 200);
//  - RESURRECT of a friendly elf: the elf stands where it fell, (230, 200).
// Each charges its price and draws exactly what the classic cast draws on
// open ground: the risen's level roll (1), none for a friend (0).
//
// RED on the unfixed tree: with the setting on the cast still reads the
// stain's box and refuses CORPSE IS BLOCKED.
TEST(GameplayFixesClericRaise, reporter_elf_against_a_wall_is_raised_with_the_setting_on)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    struct Want {
        int slot;
        unsigned char victim_team;
        int family;
        int x, y;
        float price;
        int draws;
    };
    const Want wants[] = {
        {2, 1, FAMILY_SKELETON, 218, 212, 20.0f, 1},
        {3, 1, FAMILY_GHOST, 230, 200, 50.0f, 1},
        {4, 0, FAMILY_ELF, 230, 200, 150.0f, 0},
    };
    for (const short fixes : {short{0}, short{1}}) {
        for (const Want& want : wants) {
            SCOPED_TRACE(testing::Message() << "slot " << want.slot << ", setting " << fixes);
            TestGameWorld tw;
            tw.world().gameplay_fixes = fixes;
            wall_column(tw.world(), 15);
            living* cleric = raiser(tw, 213, 200);
            ASSERT_NE(nullptr, cleric);
            walker* blood = fallen(tw, FAMILY_ELF, want.victim_team, 230, 200);
            ASSERT_NE(nullptr, blood);
            ASSERT_EQ(16, blood->sizex());
            ASSERT_EQ(0u, live_of(tw.world(), FAMILY_ELF, nullptr).size())
                << "the victim is dead";

            const Raise r = cast_raise(tw, cleric, want.slot);
            const std::vector<walker*> risen = live_of(tw.world(), want.family, cleric);
            if (fixes == 0) {
                EXPECT_FALSE(r.ok);
                EXPECT_EQ("CORPSE IS BLOCKED", r.reason);
                EXPECT_EQ(0, r.draws);
                EXPECT_FLOAT_EQ(0.0f, r.mp_spent);
                EXPECT_EQ(0, blood->dead());
                EXPECT_EQ(0u, risen.size());
                continue;
            }
            EXPECT_TRUE(r.ok) << r.reason;
            EXPECT_EQ("", r.reason);
            EXPECT_EQ(want.draws, r.draws);
            EXPECT_FLOAT_EQ(want.price, r.mp_spent);
            EXPECT_EQ(1, blood->dead());
            ASSERT_EQ(1u, risen.size());
            EXPECT_EQ(want.x, risen[0]->xpos());
            EXPECT_EQ(want.y, risen[0]->ypos());
            EXPECT_EQ(0, risen[0]->team_num());
            EXPECT_EQ(0, risen[0]->floor()) << "the stain's floor";
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The reporter's corridor: one tile wide (column 12, x 192..207, walls on
// columns 11 and 13), an elf killed 3 px off the tile grid at (195, 200),
// the cleric up the corridor at (192, 186). The stain's box reaches into the
// right wall; the skeleton fits from x 192 to 193, and the ring's first fit
// is (-2, 0) (at r = 1 the box still touches x 208, or the cleric's).
//
// RED on the unfixed tree: CORPSE IS BLOCKED with the setting on.
TEST(GameplayFixesClericRaise, reporter_one_tile_corridor)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(testing::Message() << "setting " << fixes);
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        wall_column(tw.world(), 11);
        wall_column(tw.world(), 13);
        living* cleric = raiser(tw, 192, 186);
        ASSERT_NE(nullptr, cleric);
        walker* blood = fallen(tw, FAMILY_ELF, 1, 195, 200);
        ASSERT_NE(nullptr, blood);

        const Raise r = cast_raise(tw, cleric, 2);
        const std::vector<walker*> risen = live_of(tw.world(), FAMILY_SKELETON, cleric);
        if (fixes == 0) {
            EXPECT_FALSE(r.ok);
            EXPECT_EQ("CORPSE IS BLOCKED", r.reason);
            EXPECT_EQ(0u, risen.size());
            EXPECT_EQ(0, blood->dead());
            continue;
        }
        EXPECT_TRUE(r.ok) << r.reason;
        EXPECT_EQ(1, r.draws);
        EXPECT_FLOAT_EQ(20.0f, r.mp_spent);
        ASSERT_EQ(1u, risen.size());
        EXPECT_EQ(193, risen[0]->xpos());
        EXPECT_EQ(200, risen[0]->ypos());
        EXPECT_EQ(1, blood->dead());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The reporter's "cast from on top of the stain": the cleric (16 x 12) at
// (200, 200) over an elf's stain at (200, 200), open ground. The cleric's own
// body blocks the classic probe. With the setting on the body steps off the
// cleric: the first fit is (0, +12) for the skeleton (15 x 13) and the ghost
// (13 x 14) alike, (200, 212), just under the cleric's box (y 200..211); at
// r = 12 the three offsets before it still meet the cleric.
//
// RED on the unfixed tree: CORPSE IS BLOCKED with the setting on.
TEST(GameplayFixesClericRaise, caster_standing_on_the_corpse)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        for (const int slot : {2, 3}) {
            SCOPED_TRACE(testing::Message() << "slot " << slot << ", setting " << fixes);
            TestGameWorld tw;
            tw.world().gameplay_fixes = fixes;
            living* cleric = raiser(tw, 200, 200);
            ASSERT_NE(nullptr, cleric);
            ASSERT_EQ(16, cleric->sizex());
            ASSERT_EQ(12, cleric->sizey());
            walker* blood = fallen(tw, FAMILY_ELF, 1, 200, 200);
            ASSERT_NE(nullptr, blood);

            const Raise r = cast_raise(tw, cleric, slot);
            const std::vector<walker*> risen =
                live_of(tw.world(), risen_family(slot), cleric);
            if (fixes == 0) {
                EXPECT_FALSE(r.ok);
                EXPECT_EQ("CORPSE IS BLOCKED", r.reason);
                EXPECT_EQ(0u, risen.size());
                continue;
            }
            EXPECT_TRUE(r.ok) << r.reason;
            EXPECT_EQ(1, r.draws);
            ASSERT_EQ(1u, risen.size());
            EXPECT_EQ(200, risen[0]->xpos());
            EXPECT_EQ(212, risen[0]->ypos());
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Two stains: the nearer boxed in, a farther one in the open. A pocket of
// grass one tile high (row 12, y 192..207) between wall rows 11 and 13 and a
// wall column 15 (columns 13..15 walled above and below), open to the west;
// the cleric (16 x 12) at (213, 194) in its mouth. The near stain (229, 194),
// squared centre distance 260, reaches into column 15; the 14 px between the
// cleric and the wall hold neither a skeleton (15 wide) nor a soldier (16).
// The far stain (190, 194), distance 533 (the finder's cap is 800),
// Manhattan 23: inside RAISE UNDEAD's 60 and RESURRECT's 30, in the open.
// The classic cast reads the near stain alone and refuses; with the setting
// on the far stain rises at its corner and the near one stays.
//
// RED on the unfixed tree: CORPSE IS BLOCKED with the setting on.
TEST(GameplayFixesClericRaise, a_blocked_near_stain_does_not_hide_a_usable_far_one)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    struct Case {
        int slot;
        int victim;
        unsigned char team;
        int risen;
    };
    // RAISE UNDEAD over two fallen foes; RESURRECT over two fallen friends
    // (soldiers: a 10 px elf would fit beside the cleric).
    const Case cases[] = {
        {2, FAMILY_ELF, 1, FAMILY_SKELETON},
        {4, FAMILY_SOLDIER, 0, FAMILY_SOLDIER},
    };
    for (const short fixes : {short{0}, short{1}}) {
        for (const Case& c : cases) {
            SCOPED_TRACE(testing::Message() << "slot " << c.slot << ", setting " << fixes);
            TestGameWorld tw;
            tw.world().gameplay_fixes = fixes;
            GameWorld& w = tw.world();
            for (const int col : {13, 14, 15}) {
                wall_cell(w, col, 11);
                wall_cell(w, col, 13);
            }
            wall_cell(w, 15, 12);
            living* cleric = raiser(tw, 213, 194);
            ASSERT_NE(nullptr, cleric);
            walker* near_blood = fallen(tw, c.victim, c.team, 229, 194);
            walker* far_blood = fallen(tw, c.victim, c.team, 190, 194);
            ASSERT_TRUE(near_blood && far_blood);
            ASSERT_EQ(near_blood, w.find_nearest_blood(cleric));
            ASSERT_EQ(533, cleric->distance_to_ob_center(far_blood));
            ASSERT_EQ(23, cleric->distance_to_ob(far_blood));

            const Raise r = cast_raise(tw, cleric, c.slot);
            const std::vector<walker*> risen = live_of(w, c.risen, cleric);
            if (fixes == 0) {
                EXPECT_FALSE(r.ok);
                EXPECT_EQ("CORPSE IS BLOCKED", r.reason);
                EXPECT_EQ(0u, risen.size());
                EXPECT_EQ(0, far_blood->dead());
                continue;
            }
            EXPECT_TRUE(r.ok) << r.reason;
            EXPECT_EQ(0, near_blood->dead()) << "the boxed-in corpse stays";
            EXPECT_EQ(1, far_blood->dead());
            ASSERT_EQ(1u, risen.size());
            EXPECT_EQ(190, risen[0]->xpos());
            EXPECT_EQ(194, risen[0]->ypos());
            EXPECT_EQ(0, risen[0]->team_num());
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// RESURRECT and two stains in the open: a foe's elf at (200, 200), squared
// centre distance 293, and a friendly elf farther off at (170, 214), 425.
// The nearest usable stain decides friend or foe, as in the classic cast: the
// foe rises as a ghost of the cleric's team on its corner and the friend
// stays down, on both values. One draw: the ghost's level roll.
//
// RED (planted in corpses.lua: RESURRECT skipping a foe's stain, "friend
// first"): with the setting on the friend rises and no ghost does.
TEST(GameplayFixesClericRaise, resurrect_takes_the_nearest_usable_stain_whatever_its_team)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    for (const short fixes : {short{0}, short{1}}) {
        SCOPED_TRACE(testing::Message() << "setting " << fixes);
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = raiser(tw, 183, 200);
        ASSERT_NE(nullptr, cleric);
        walker* foe_blood = fallen(tw, FAMILY_ELF, 1, 200, 200);
        walker* friend_blood = fallen(tw, FAMILY_ELF, 0, 170, 214);
        ASSERT_TRUE(foe_blood && friend_blood);
        ASSERT_EQ(293, cleric->distance_to_ob_center(foe_blood));
        ASSERT_EQ(425, cleric->distance_to_ob_center(friend_blood));

        const Raise r = cast_raise(tw, cleric, 4);
        EXPECT_TRUE(r.ok) << r.reason;
        EXPECT_EQ(1, r.draws);
        EXPECT_FLOAT_EQ(150.0f, r.mp_spent);
        EXPECT_EQ(1, foe_blood->dead());
        EXPECT_EQ(0, friend_blood->dead());
        const std::vector<walker*> ghosts = live_of(tw.world(), FAMILY_GHOST, cleric);
        ASSERT_EQ(1u, ghosts.size());
        EXPECT_EQ(0, ghosts[0]->team_num());
        EXPECT_EQ(200, ghosts[0]->xpos());
        EXPECT_EQ(200, ghosts[0]->ypos());
        EXPECT_EQ(0u, live_of(tw.world(), FAMILY_ELF, cleric).size());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The draws of every raise, on open ground (an elf's stain at (200, 200),
// the cleric at (183, 200)): RAISE UNDEAD 1, RAISE GHOST 1, RESURRECT of a
// foe 1 (the risen's level roll), of a friend 0, the body at the corner, on
// both values. With a drumstick under the stain the classic probe eats it
// and draws once more (2, the drumstick gone); the fixed probe eats nothing
// (1, the drumstick still there).
//
// RED on the unfixed tree: with the setting on the cast still runs the
// classic probe, which eats the drumstick and draws 2.
TEST(GameplayFixesClericRaise, the_fixed_probe_eats_nothing)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    struct Kind {
        int slot;
        unsigned char victim_team;
        int risen;
        int draws;
    };
    const Kind kinds[] = {
        {2, 1, FAMILY_SKELETON, 1},
        {3, 1, FAMILY_GHOST, 1},
        {4, 1, FAMILY_GHOST, 1},
        {4, 0, FAMILY_ELF, 0},
    };
    for (const short fixes : {short{0}, short{1}}) {
        for (const Kind& k : kinds) {
            SCOPED_TRACE(testing::Message() << "slot " << k.slot << ", team "
                                            << int{k.victim_team} << ", setting " << fixes);
            TestGameWorld tw;
            tw.world().gameplay_fixes = fixes;
            living* cleric = raiser(tw, 183, 200);
            ASSERT_NE(nullptr, cleric);
            walker* blood = fallen(tw, FAMILY_ELF, k.victim_team, 200, 200);
            ASSERT_NE(nullptr, blood);
            const Raise r = cast_raise(tw, cleric, k.slot);
            EXPECT_TRUE(r.ok) << r.reason;
            EXPECT_EQ(k.draws, r.draws);
            const std::vector<walker*> risen = live_of(tw.world(), k.risen, cleric);
            ASSERT_EQ(1u, risen.size());
            EXPECT_EQ(200, risen[0]->xpos());
            EXPECT_EQ(200, risen[0]->ypos());
        }

        SCOPED_TRACE(testing::Message() << "the drumstick, setting " << fixes);
        TestGameWorld tw;
        tw.world().gameplay_fixes = fixes;
        living* cleric = raiser(tw, 183, 200);
        ASSERT_NE(nullptr, cleric);
        walker* drumstick = tw.world().add_ob(Order::Treasure, FAMILY_DRUMSTICK);
        ASSERT_NE(nullptr, drumstick);
        drumstick->setxy(202, 202);
        drumstick->stats()->set_level(1);
        walker* blood = fallen(tw, FAMILY_ELF, 1, 200, 200);
        ASSERT_NE(nullptr, blood);
        const Raise r = cast_raise(tw, cleric, 2);
        EXPECT_TRUE(r.ok) << r.reason;
        EXPECT_EQ(fixes == 0 ? 2 : 1, r.draws);
        EXPECT_EQ(fixes == 0 ? 1 : 0, drumstick->dead());
        const std::vector<walker*> risen = live_of(tw.world(), FAMILY_SKELETON, cleric);
        ASSERT_EQ(1u, risen.size());
        EXPECT_EQ(200, risen[0]->xpos());
        EXPECT_EQ(200, risen[0]->ypos());
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Every refusal, on both values, spends nothing: no MP, no busy, no event,
// no draw, the stain still there, nothing risen.
//  - NO CORPSE NEARBY: no stain at all;
//  - CORPSE IS BLOCKED: a live golem (48 x 36, team 0) at (180, 190) over a
//    foe's stain at (200, 200): it meets the stain's box and every body any
//    raise could stand within 16 px of the corner (the cleric at (200, 180));
//  - CORPSE TOO FAR (RAISE GHOST and RESURRECT, 30 px): a stain 16 px right
//    and 15 px down, Manhattan 31, which the finder still answers.
//
// RED (planted: corpses.lua without `blocked = true`): with the setting on
// the golem case reads CORPSE TOO FAR.
TEST(GameplayFixesClericRaise, refusals_spend_nothing_on_both_values)
{
    og::test::mount_core_pack();
    og::test::ScopedHookFailureGuard guard;
    enum class Rig { None, Golem, Far };
    struct Case {
        Rig rig;
        int slot;
        const char* reason;
    };
    const Case cases[] = {
        {Rig::None, 2, "NO CORPSE NEARBY"},   {Rig::None, 3, "NO CORPSE NEARBY"},
        {Rig::None, 4, "NO CORPSE NEARBY"},   {Rig::Golem, 2, "CORPSE IS BLOCKED"},
        {Rig::Golem, 3, "CORPSE IS BLOCKED"}, {Rig::Golem, 4, "CORPSE IS BLOCKED"},
        {Rig::Far, 3, "CORPSE TOO FAR"},      {Rig::Far, 4, "CORPSE TOO FAR"},
    };
    for (const short fixes : {short{0}, short{1}}) {
        for (const Case& c : cases) {
            SCOPED_TRACE(testing::Message() << c.reason << ", slot " << c.slot
                                            << ", setting " << fixes);
            TestGameWorld tw;
            tw.world().gameplay_fixes = fixes;
            living* cleric = raiser(tw, 200, 180);
            ASSERT_NE(nullptr, cleric);
            walker* blood = nullptr;
            if (c.rig == Rig::Golem) {
                living* golem = add_living(tw, FAMILY_GOLEM, 0, 180, 190, 1);
                ASSERT_NE(nullptr, golem);
                ASSERT_EQ(48, golem->sizex());
                ASSERT_EQ(36, golem->sizey());
                blood = fallen(tw, FAMILY_ELF, 1, 200, 200);
                ASSERT_NE(nullptr, blood);
            } else if (c.rig == Rig::Far) {
                blood = fallen(tw, FAMILY_ELF, 1, 216, 195);
                ASSERT_NE(nullptr, blood);
                ASSERT_EQ(31, cleric->distance_to_ob(blood));
                ASSERT_EQ(blood, tw.world().find_nearest_blood(cleric));
            }
            const std::size_t livings = tw.world().oblist.size();

            const Raise r = cast_raise(tw, cleric, c.slot);
            EXPECT_FALSE(r.ok);
            EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, r.why);
            EXPECT_EQ(c.reason, r.reason);
            EXPECT_EQ(0, r.draws);
            EXPECT_FLOAT_EQ(0.0f, r.mp_spent);
            EXPECT_FLOAT_EQ(0.0f, cleric->busy());
            EXPECT_EQ(0u, r.events);
            EXPECT_EQ(livings, tw.world().oblist.size()) << "nothing risen";
            if (blood != nullptr) {
                EXPECT_EQ(0, blood->dead());
            }
        }
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
