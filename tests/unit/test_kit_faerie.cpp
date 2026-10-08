/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Specials, faerie: BLINK / SWAP, GLIMMER, HASTEN / HASTE SELF, WISH
// (packs/core/lib/kit_faerie.lua, declared in living-07-faerie.lua).
//
// Every cast goes through the real walker::special(), so the price gate and
// the charge are the engine's. Each ON case has an OFF twin on the same
// staging: with the setting off the faerie has no specials at all, so every
// press fails for want of mana (the hidden slot prices at 5000) and nothing
// moves, spawns or draws.
//
// Each special's test names the one edit of the staged copy
// (build/ci-test/packs/core/...) that was made once, by hand, to prove the
// test can fail; the failure lines it printed are quoted beside it.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/core/sound_ids.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

using Failure = walker::SpecialFailure;

const FamilyDescriptor* faerie_fd()
{
    return get_family_descriptor(FAMILY_FAERIE);
}

living* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->set_real_team_num(255);
    w->setxy(x, y);
    return dynamic_cast<living*>(w);
}

// A team-1 faerie of `level` with `mp` magic, standing at (x, y).
living* add_faerie(TestGameWorld& tw, short level, float mp, short x = 160,
                   short y = 160)
{
    living* f = add_living(tw, FAMILY_FAERIE, 1, x, y);
    if (f == nullptr)
        return nullptr;
    f->stats()->set_level(level);
    f->stats()->set_max_magicpoints(mp);
    f->stats()->set_magicpoints(mp);
    return f;
}

void set_hp(walker* w, float hp, float max_hp)
{
    w->stats()->set_max_hitpoints(max_hp);
    w->stats()->set_hitpoints(hp);
}

walker* add_stain(TestGameWorld& tw, unsigned char team, short x, short y,
                  char old_family, short level, float max_hp)
{
    walker* stain = tw.world().add_fx_ob(Order::Treasure, FAMILY_STAIN);
    if (stain == nullptr)
        return nullptr;
    stain->set_team_num(team);
    stain->setxy(x, y);
    stain->set_dead(0);
    stain->stats()->set_old_family(old_family);
    stain->stats()->set_level(level);
    // A real stain carries the body's stats (generate_bloodspot).
    stain->stats()->set_max_hitpoints(max_hp);
    stain->stats()->set_hitpoints(-5.0f);
    return stain;
}

struct Cast {
    bool ok = false;
    Failure why = Failure::None;
    std::string reason;
};

Cast cast(walker* self, int slot, bool shift)
{
    self->set_current_special(static_cast<char>(slot));
    self->set_shifter_down(shift ? 1 : 0);
    Cast out;
    out.ok = self->special(&out.why, &out.reason);
    return out;
}

int count_live(const GameWorld::EntityList& list, Order order, int family)
{
    int n = 0;
    for (const auto& uptr : list)
        if (uptr && !uptr->dead() && uptr->query_order() == order &&
            uptr->family() == family)
            ++n;
    return n;
}

int flashes(TestGameWorld& tw)
{
    return count_live(tw.world().oblist, Order::FX, FAMILY_FLASH);
}

int sprinkles(TestGameWorld& tw)
{
    return count_live(tw.world().weaplist, Order::Weapon, FAMILY_SPRINKLE);
}

int sounds(TestGameWorld& tw, std::uint32_t sound)
{
    int n = 0;
    for (const auto& ev : tw.events.events())
        if (ev.kind == og::sim::EventKind::PlaySound && ev.a == sound)
            ++n;
    return n;
}

void set_tile(TestGameWorld& tw, int col, int row, int tile)
{
    GameWorld& w = tw.world();
    w.grid.data[static_cast<std::size_t>(col + row * w.grid.w)] =
        static_cast<unsigned char>(tile);
}

}  // namespace

// The faerie had no specials. With the setting on she has four, opening at
// levels 1, 4, 7 and 10 like every class's; with it off she has none, on
// the HUD view, the price and the player's special-cycling key alike.
//
// Proof it can fail: dropping GLIMMER's `new_kit = true` in the staged
// living-07-faerie.lua printed
//   Expected equality of these values: "NONE"
//   og::sim::special_name(fd, row.slot, 0) Which is: "GLIMMER"
//   Expected equality of these values: 5000
//   og::sim::cast_cost(*f, row.slot, false) Which is: 24
TEST(KitFaerie, four_slots_open_at_levels_1_4_7_10_and_none_when_off)
{
    const FamilyDescriptor* fd = faerie_fd();
    ASSERT_NE(nullptr, fd);
    struct Row {
        int slot;
        const char* name;
        const char* alternate;
        unsigned short cost;
        unsigned short shifted_cost;
    };
    const Row rows[] = {
        {1, "BLINK", "SWAP", 8, 20},
        {2, "GLIMMER", "NONE", 24, 24},
        {3, "HASTEN", "HASTE SELF", 30, 30},
        {4, "WISH", "NONE", 100, 100},
    };
    TestGameWorld tw;
    living* f = add_faerie(tw, 10, 500.0f);
    ASSERT_NE(nullptr, f);
    for (const Row& row : rows) {
        EXPECT_STREQ(row.name, og::sim::special_name(fd, row.slot, 1));
        EXPECT_STREQ(row.alternate, og::sim::alternate_name(fd, row.slot, 1));
        EXPECT_STREQ("NONE", og::sim::special_name(fd, row.slot, 0));
        EXPECT_STREQ("NONE", og::sim::alternate_name(fd, row.slot, 0));
        tw.world().new_specials = 1;
        EXPECT_EQ(row.cost, og::sim::cast_cost(*f, row.slot, false)) << row.name;
        EXPECT_EQ(row.shifted_cost, og::sim::cast_cost(*f, row.slot, true))
            << row.name;
        tw.world().new_specials = 0;
        EXPECT_EQ(5000, og::sim::cast_cost(*f, row.slot, false)) << row.name;
        EXPECT_EQ(5000, og::sim::cast_cost(*f, row.slot, true)) << row.name;
    }

    // The cycling key walks only the slots the faerie's level has opened.
    const auto walk = [&](short level, short flag) {
        tw.world().new_specials = flag;
        f->stats()->set_level(level);
        f->set_current_special(1);
        std::string seen = "1";
        for (int press = 0; press < 4; ++press) {
            sim_advance_current_special(tw.world(), *f);
            seen += std::to_string(static_cast<int>(f->current_special()));
        }
        return seen;
    };
    EXPECT_EQ("11111", walk(1, 1));
    EXPECT_EQ("12121", walk(4, 1));
    EXPECT_EQ("12312", walk(7, 1));
    EXPECT_EQ("12341", walk(10, 1));
    EXPECT_EQ("11111", walk(10, 0)) << "no specials with the setting off";
}

// WISH is a level-10 price on a level-10 slot: the faerie's intelligence
// grows by 8 a level (her level_up), so her pool, 10 + 3 x intelligence,
// is 52 at level 1 and 268 at level 10.
TEST(KitFaerie, faerie_levels_into_her_wish_budget)
{
    const FamilyDescriptor* fd = faerie_fd();
    ASSERT_NE(nullptr, fd);
    guy faerie(FAMILY_FAERIE);
    ASSERT_EQ(14, faerie.intelligence);
    og::test::level_up(*fd, &faerie, 9);
    EXPECT_EQ(14 + 9 * 8, faerie.intelligence);
    EXPECT_EQ(268, 10 + 3 * faerie.intelligence);
}

// BLINK: the engine's ranged hop with range blink_base + blink_per_level *
// level (24 + 6 at level 1), a flash where she left and where she landed,
// then the appear row, which refuses a held key until it has played.
//
// Proof it can fail: blink_base = 24 -> 0 in the staged living-07-faerie.lua
// printed
//   Expected equality of these values: expected_x Which is: 134
//   f->xpos() Which is: 158
TEST(KitFaerie, blink_moves_within_range_and_flashes_then_is_busy_while_appearing)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 1, 52.0f);
    ASSERT_NE(nullptr, f);

    // teleport_ranged draws x then y from next(2 * range), first try lands
    // on open grass.
    const int range = 24 + 6 * 1;
    og::sim::SimRandom probe(tw.world().rng_.state_);
    const int expected_x = static_cast<int>(probe.next(2 * range)) - range + 160;
    const int expected_y = static_cast<int>(probe.next(2 * range)) - range + 160;

    const Cast first = cast(f, 1, false);
    ASSERT_TRUE(first.ok) << first.reason;
    EXPECT_EQ(expected_x, f->xpos());
    EXPECT_EQ(expected_y, f->ypos());
    EXPECT_EQ(probe.state_, tw.world().rng_.state_) << "two draws, no more";
    EXPECT_FLOAT_EQ(44.0f, f->stats()->magicpoints()) << "BLINK costs 8";
    EXPECT_EQ(2, flashes(tw)) << "a flash where she left and where she landed";
    EXPECT_EQ(ANI_TELE_IN, f->ani_type());
    EXPECT_EQ(0, f->cycle());

    // The next tick's press (a held key) meets the appear row.
    const Cast held = cast(f, 1, false);
    EXPECT_FALSE(held.ok);
    EXPECT_EQ(Failure::ScriptDeclined, held.why);
    EXPECT_EQ("SPECIAL BUSY", held.reason);
    EXPECT_FLOAT_EQ(44.0f, f->stats()->magicpoints()) << "a refusal is free";
    EXPECT_EQ(expected_x, f->xpos());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Walled in on every side: the hop finds no landing in its tries, so BLINK
// refuses and nothing is spent, flashed or started.
TEST(KitFaerie, blink_with_nowhere_to_land_refuses_and_spends_nothing)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 1, 52.0f);
    ASSERT_NE(nullptr, f);
    GameWorld& w = tw.world();
    for (int i = 0; i < w.grid.w * w.grid.h; ++i)
        w.grid.data[static_cast<std::size_t>(i)] =
            static_cast<unsigned char>(PIX_H_WALL1);
    const Cast walled = cast(f, 1, false);
    EXPECT_FALSE(walled.ok);
    EXPECT_EQ(Failure::ScriptDeclined, walled.why);
    EXPECT_EQ("NOWHERE TO BLINK", walled.reason);
    EXPECT_EQ(160, f->xpos());
    EXPECT_EQ(160, f->ypos());
    EXPECT_FLOAT_EQ(52.0f, f->stats()->magicpoints());
    EXPECT_EQ(0, flashes(tw));
    EXPECT_NE(ANI_TELE_IN, f->ani_type());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitFaerie, blink_is_no_special_when_off)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    living* f = add_faerie(tw, 1, 52.0f);
    ASSERT_NE(nullptr, f);
    const std::uint32_t rng_before = tw.world().rng_.state_;
    const Cast off = cast(f, 1, false);
    EXPECT_FALSE(off.ok);
    EXPECT_EQ(Failure::NoMP, off.why);
    EXPECT_EQ(160, f->xpos());
    EXPECT_EQ(160, f->ypos());
    EXPECT_FLOAT_EQ(52.0f, f->stats()->magicpoints());
    EXPECT_EQ(0, flashes(tw));
    EXPECT_EQ(rng_before, tw.world().rng_.state_);
    EXPECT_NE(ANI_TELE_IN, f->ani_type());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// SWAP (shift): trade places with the nearest walker, ally or foe, in line
// of sight within swap_range (80). A wall between hides the nearer foe, so
// the farther ally is taken; with no one in sight it refuses for free; a
// ground walker is never dropped onto water the faerie hovers over.
//
// Proof it can fail: swap_range = 80 -> 0 in the staged living-07-faerie.lua
// printed
//   Value of: swapped.ok  Actual: false  Expected: true
// and `return og.line_clear(self, ob)` -> `return true` in the staged
// kit_faerie.lua printed
//   Expected equality of these values: 160 f->xpos() Which is: 192
//   Expected equality of these values: 192 walled->xpos() Which is: 160
// (she took the walled foe's place instead).
TEST(KitFaerie, swap_trades_places_only_in_line_of_sight)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 1, 52.0f, 160, 160);
    living* walled = add_living(tw, FAMILY_SOLDIER, 0, 192, 160);  // 32 away
    living* ally = add_living(tw, FAMILY_SOLDIER, 1, 160, 208);    // 48 away
    ASSERT_TRUE(f && walled && ally);
    set_tile(tw, 11, 10, PIX_H_WALL1);  // between the faerie and the foe
    ASSERT_FALSE(tw.world().clear_sight_line(f, walled));
    ASSERT_TRUE(tw.world().clear_sight_line(f, ally));

    const Cast swapped = cast(f, 1, true);
    ASSERT_TRUE(swapped.ok) << swapped.reason;
    EXPECT_EQ(160, f->xpos());
    EXPECT_EQ(208, f->ypos()) << "the faerie stands where the ally stood";
    EXPECT_EQ(160, ally->xpos());
    EXPECT_EQ(160, ally->ypos()) << "the ally stands where the faerie was";
    EXPECT_EQ(192, walled->xpos()) << "the foe behind the wall is untouched";
    EXPECT_FLOAT_EQ(32.0f, f->stats()->magicpoints()) << "SWAP costs 20";
    EXPECT_EQ(2, flashes(tw));
    EXPECT_NE(ANI_TELE_IN, f->ani_type()) << "a swap is not a blink";

    // Alone in sight: refused, nothing spent.
    TestGameWorld lonely;
    lonely.world().new_specials = 1;
    living* alone = add_faerie(lonely, 1, 52.0f, 160, 160);
    living* far_foe = add_living(lonely, FAMILY_SOLDIER, 0, 260, 160);  // 100
    ASSERT_TRUE(alone && far_foe);
    const Cast nobody = cast(alone, 1, true);
    EXPECT_FALSE(nobody.ok);
    EXPECT_EQ(Failure::ScriptDeclined, nobody.why);
    EXPECT_EQ("NO ONE IN SIGHT", nobody.reason);
    EXPECT_FLOAT_EQ(52.0f, alone->stats()->magicpoints());
    EXPECT_EQ(160, alone->xpos());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitFaerie, swap_never_drops_a_walker_into_water_she_hovers_over)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 1, 52.0f, 320, 320);
    living* foe = add_living(tw, FAMILY_SOLDIER, 0, 320, 352);
    ASSERT_TRUE(f && foe);
    set_tile(tw, 20, 20, PIX_WATER1);  // under the faerie only
    ASSERT_FALSE(tw.world().query_grid_passable(320, 320, foe));
    ASSERT_TRUE(tw.world().clear_sight_line(f, foe));

    const Cast blocked = cast(f, 1, true);
    EXPECT_FALSE(blocked.ok);
    EXPECT_EQ(Failure::ScriptDeclined, blocked.why);
    EXPECT_EQ("SWAP BLOCKED", blocked.reason);
    EXPECT_EQ(320, f->ypos());
    EXPECT_EQ(352, foe->ypos());
    EXPECT_FLOAT_EQ(52.0f, f->stats()->magicpoints());
    EXPECT_EQ(0, flashes(tw));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitFaerie, swap_is_no_special_when_off)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    living* f = add_faerie(tw, 1, 52.0f, 160, 160);
    living* ally = add_living(tw, FAMILY_SOLDIER, 1, 160, 208);
    ASSERT_TRUE(f && ally);
    const Cast off = cast(f, 1, true);
    EXPECT_FALSE(off.ok);
    EXPECT_EQ(Failure::NoMP, off.why) << "SWAP hangs off a hidden slot";
    EXPECT_EQ(160, f->ypos());
    EXPECT_EQ(208, ally->ypos());
    EXPECT_FLOAT_EQ(52.0f, f->stats()->magicpoints());
    EXPECT_EQ(0, flashes(tw));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// GLIMMER: eight sprinkles, one per direction; the shots are pre-paid, so
// the burst costs exactly its 24; the aim is put back; the burst pauses the
// faerie like one attack, so the next press is refused.
//
// Proof it can fail: `for j = -1, 1 do` -> `for j = -1, 0 do` in the staged
// kit_faerie.lua printed
//   Expected equality of these values: 8 sprinkles(tw) Which is: 5
//   Expected equality of these values: 76.0f f->stats()->magicpoints() Which is: 82
TEST(KitFaerie, glimmer_fires_eight_sprinkles_and_prepays)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 4, 100.0f);
    ASSERT_NE(nullptr, f);
    f->set_lastx(1);
    f->set_lasty(0);
    f->set_busy(0.0f);
    const float shot_cost = static_cast<float>(f->stats()->weapon_cost());
    ASSERT_GT(shot_cost, 0.0f) << "the sprinkle has a price the burst pre-pays";

    const Cast burst = cast(f, 2, false);
    ASSERT_TRUE(burst.ok) << burst.reason;
    EXPECT_EQ(8, sprinkles(tw));
    EXPECT_FLOAT_EQ(76.0f, f->stats()->magicpoints()) << "GLIMMER costs 24";
    EXPECT_FLOAT_EQ(1.0f, f->lastx());
    EXPECT_FLOAT_EQ(0.0f, f->lasty());
    EXPECT_FLOAT_EQ(f->fire_frequency(), f->busy());

    const Cast held = cast(f, 2, false);
    EXPECT_FALSE(held.ok);
    EXPECT_EQ("SPECIAL BUSY", held.reason);
    EXPECT_EQ(8, sprinkles(tw));
    EXPECT_FLOAT_EQ(76.0f, f->stats()->magicpoints());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitFaerie, glimmer_is_no_special_when_off)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    living* f = add_faerie(tw, 4, 100.0f);
    ASSERT_NE(nullptr, f);
    const Cast off = cast(f, 2, false);
    EXPECT_FALSE(off.ok);
    EXPECT_EQ(Failure::NoMP, off.why);
    EXPECT_EQ(0, sprinkles(tw));
    EXPECT_FLOAT_EQ(100.0f, f->stats()->magicpoints());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// HASTEN: the nearest ally within 40 gets the speed potion's timer (+150
// ticks, stacking) and a bonus of 2 px a tick, never lowering a stronger one
// already running; HASTE SELF (shift) is the faerie herself, same price.
//
// Proof it can fail: hasten_bonus = 2 -> 0 in the staged living-07-faerie.lua
// printed
//   Expected equality of these values: 2.0f near->speed_bonus() Which is: 0
//   Expected equality of these values: 2.0f f->speed_bonus() Which is: 0
TEST(KitFaerie, hasten_ally_or_self_by_shift)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 7, 200.0f, 160, 160);
    living* near = add_living(tw, FAMILY_SOLDIER, 1, 184, 160);   // 24
    living* other = add_living(tw, FAMILY_SOLDIER, 1, 160, 196);  // 36
    living* foe = add_living(tw, FAMILY_SOLDIER, 0, 170, 160);    // 10
    ASSERT_TRUE(f && near && other && foe);

    const Cast ally_cast = cast(f, 3, false);
    ASSERT_TRUE(ally_cast.ok) << ally_cast.reason;
    EXPECT_EQ(150, near->speed_bonus_left());
    EXPECT_FLOAT_EQ(2.0f, near->speed_bonus());
    EXPECT_EQ(0, other->speed_bonus_left()) << "only the nearest ally";
    EXPECT_EQ(0, foe->speed_bonus_left()) << "never a foe";
    EXPECT_EQ(0, f->speed_bonus_left()) << "not herself without the shift";
    EXPECT_FLOAT_EQ(170.0f, f->stats()->magicpoints()) << "HASTEN costs 30";
    EXPECT_EQ(1, sounds(tw, SOUND_HEAL));
    EXPECT_EQ(1, count_live(tw.world().oblist, Order::FX, FAMILY_HIT));

    // Again: the timer stacks; a stronger potion bonus is kept.
    near->set_speed_bonus(5.0f);
    ASSERT_TRUE(cast(f, 3, false).ok);
    EXPECT_EQ(300, near->speed_bonus_left());
    EXPECT_FLOAT_EQ(5.0f, near->speed_bonus());

    const Cast self_cast = cast(f, 3, true);
    ASSERT_TRUE(self_cast.ok) << self_cast.reason;
    EXPECT_EQ(150, f->speed_bonus_left());
    EXPECT_FLOAT_EQ(2.0f, f->speed_bonus());
    EXPECT_EQ(0, other->speed_bonus_left());
    EXPECT_FLOAT_EQ(110.0f, f->stats()->magicpoints()) << "HASTE SELF costs 30";

    // No ally in reach: refused, free.
    TestGameWorld lonely;
    lonely.world().new_specials = 1;
    living* alone = add_faerie(lonely, 7, 200.0f, 160, 160);
    living* far_ally = add_living(lonely, FAMILY_SOLDIER, 1, 160, 210);  // 50
    ASSERT_TRUE(alone && far_ally);
    const Cast nobody = cast(alone, 3, false);
    EXPECT_FALSE(nobody.ok);
    EXPECT_EQ("NO ALLY IN REACH", nobody.reason);
    EXPECT_FLOAT_EQ(200.0f, alone->stats()->magicpoints());
    EXPECT_EQ(0, far_ally->speed_bonus_left());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitFaerie, hasten_is_no_special_when_off)
{
    og::test::ScopedHookFailureGuard guard;
    TestGameWorld tw;
    living* f = add_faerie(tw, 7, 200.0f, 160, 160);
    living* near = add_living(tw, FAMILY_SOLDIER, 1, 184, 160);
    ASSERT_TRUE(f && near);
    for (const bool shift : {false, true}) {
        const Cast off = cast(f, 3, shift);
        EXPECT_FALSE(off.ok);
        EXPECT_EQ(Failure::NoMP, off.why);
    }
    EXPECT_EQ(0, near->speed_bonus_left());
    EXPECT_EQ(0, f->speed_bonus_left());
    EXPECT_FLOAT_EQ(200.0f, f->stats()->magicpoints());
    EXPECT_EQ(0, sounds(tw, SOUND_HEAL));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

namespace {

struct WishStage {
    TestGameWorld tw;
    living* faerie = nullptr;
    walker* near_stain = nullptr;   // friendly, 40 away: the one raised
    walker* far_stain = nullptr;    // friendly, 100 away
    walker* foe_stain = nullptr;    // the foe's, 16 away
    living* hurt_a = nullptr;       // ally, 40 away, 20/100
    living* hurt_b = nullptr;       // ally, 30 away, 10/80
    living* hurt_far = nullptr;     // ally, 240 away, 10/100
    living* hurt_foe = nullptr;     // foe, 40 away, 10/100

    explicit WishStage(short new_specials)
    {
        tw.world().new_specials = new_specials;
        faerie = add_faerie(tw, 10, 300.0f, 160, 160);
        near_stain = add_stain(tw, 1, 200, 160, FAMILY_SOLDIER, 3, 120.0f);
        far_stain = add_stain(tw, 1, 260, 160, FAMILY_SOLDIER, 3, 120.0f);
        foe_stain = add_stain(tw, 0, 176, 160, FAMILY_SOLDIER, 3, 120.0f);
        hurt_a = add_living(tw, FAMILY_SOLDIER, 1, 160, 200);
        hurt_b = add_living(tw, FAMILY_SOLDIER, 1, 130, 160);
        hurt_far = add_living(tw, FAMILY_SOLDIER, 1, 400, 160);
        hurt_foe = add_living(tw, FAMILY_SOLDIER, 0, 120, 160);
        if (hurt_a && hurt_b && hurt_far && hurt_foe) {
            set_hp(hurt_a, 20.0f, 100.0f);
            set_hp(hurt_b, 10.0f, 80.0f);
            set_hp(hurt_far, 10.0f, 100.0f);
            set_hp(hurt_foe, 10.0f, 100.0f);
        }
    }

    bool ready() const
    {
        return faerie && near_stain && far_stain && foe_stain && hurt_a &&
               hurt_b && hurt_far && hurt_foe;
    }

    // The living raised at (200, 160), or nullptr.
    walker* raised()
    {
        for (const auto& uptr : tw.world().oblist)
            if (uptr && !uptr->dead() && uptr->query_order() == Order::Living &&
                uptr->xpos() == 200 && uptr->ypos() == 160)
                return uptr.get();
        return nullptr;
    }
};

}  // namespace

// WISH: the nearest friendly stain within 120 rises at FULL health (the
// cleric's friendly resurrect, so the body's own stats come back), every
// living ally within 96 is healed to full, and the faerie pops.
//
// Proof it can fail: wish_range = 120 -> 0 in the staged living-07-faerie.lua
// printed
//   Value of: wished.ok  Actual: false  Expected: true
TEST(KitFaerie, wish_raises_the_nearest_fallen_ally_at_full_and_heals_the_party_then_pops)
{
    og::test::ScopedHookFailureGuard guard;
    WishStage s(1);
    ASSERT_TRUE(s.ready());

    const Cast wished = cast(s.faerie, 4, false);
    ASSERT_TRUE(wished.ok) << wished.reason;

    walker* risen = s.raised();
    ASSERT_NE(nullptr, risen) << "a fallen ally rises where it fell";
    EXPECT_EQ(FAMILY_SOLDIER, risen->family());
    EXPECT_EQ(1, risen->team_num());
    EXPECT_EQ(3, risen->stats()->level());
    EXPECT_FLOAT_EQ(120.0f, risen->stats()->max_hitpoints());
    EXPECT_FLOAT_EQ(120.0f, risen->stats()->hitpoints()) << "at full health";
    EXPECT_TRUE(s.near_stain->dead()) << "the corpse is used up";
    EXPECT_FALSE(s.far_stain->dead()) << "only the nearest";
    EXPECT_FALSE(s.foe_stain->dead()) << "never a foe's corpse";

    EXPECT_FLOAT_EQ(100.0f, s.hurt_a->stats()->hitpoints());
    EXPECT_FLOAT_EQ(80.0f, s.hurt_b->stats()->hitpoints());
    EXPECT_FLOAT_EQ(10.0f, s.hurt_far->stats()->hitpoints()) << "out of radius";
    EXPECT_FLOAT_EQ(10.0f, s.hurt_foe->stats()->hitpoints()) << "never a foe";

    EXPECT_TRUE(s.faerie->dead()) << "the faerie pops";
    EXPECT_EQ(1, sounds(s.tw, SOUND_HEAL));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitFaerie, wish_with_no_fallen_ally_refuses_and_spends_nothing)
{
    og::test::ScopedHookFailureGuard guard;
    WishStage s(1);
    ASSERT_TRUE(s.ready());
    s.near_stain->set_dead(1);
    // The far stain sits exactly at the range (Manhattan 120): too far.
    s.far_stain->setxy(280, 160);

    const Cast refused = cast(s.faerie, 4, false);
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ(Failure::ScriptDeclined, refused.why);
    EXPECT_EQ("NO FALLEN ALLY NEARBY", refused.reason);
    EXPECT_FALSE(s.faerie->dead());
    EXPECT_FLOAT_EQ(300.0f, s.faerie->stats()->magicpoints());
    EXPECT_FLOAT_EQ(20.0f, s.hurt_a->stats()->hitpoints()) << "no heal either";
    EXPECT_FALSE(s.far_stain->dead());
    EXPECT_FALSE(s.foe_stain->dead());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitFaerie, wish_is_no_special_when_off)
{
    og::test::ScopedHookFailureGuard guard;
    WishStage s(0);
    ASSERT_TRUE(s.ready());
    const Cast off = cast(s.faerie, 4, false);
    EXPECT_FALSE(off.ok);
    EXPECT_EQ(Failure::NoMP, off.why);
    EXPECT_FALSE(s.faerie->dead());
    EXPECT_EQ(nullptr, s.raised());
    EXPECT_FALSE(s.near_stain->dead());
    EXPECT_FLOAT_EQ(20.0f, s.hurt_a->stats()->hitpoints());
    EXPECT_FLOAT_EQ(300.0f, s.faerie->stats()->magicpoints());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The bot gates.

// With the setting off every slot's gate answers true at once, as the
// classic faerie (no gate at all) did, touching nothing: no draw, no shift
// written, no foe acquired. The engine asks slot 1's gate of every hit
// faerie bot, so this is what keeps the classic game's draws in step.
//
// Proof it can fail: deleting the setting check from ai_glimmer in the
// staged kit_faerie.lua printed
//   Value of: og::test::check_special_ai(*fd, f)  Actual: false  Expected: true
// (twice: slot 2, shift 0 and shift 1).
TEST(KitFaerie, ai_off_answers_the_classic_gate)
{
    og::test::ScopedHookFailureGuard guard;
    const FamilyDescriptor* fd = faerie_fd();
    ASSERT_NE(nullptr, fd);
    TestGameWorld tw;
    living* f = add_faerie(tw, 10, 500.0f);
    ASSERT_NE(nullptr, f);
    // Alone and unhurt: with the setting on, every gate would say no.
    for (int slot = 1; slot <= 4; ++slot) {
        f->set_current_special(static_cast<char>(slot));
        for (const short shift : {short{0}, short{1}}) {
            f->set_shifter_down(shift);
            const std::uint32_t before = tw.world().rng_.state_;
            EXPECT_TRUE(og::test::check_special_ai(*fd, f)) << "slot " << slot;
            EXPECT_EQ(before, tw.world().rng_.state_) << "slot " << slot;
            EXPECT_EQ(shift, f->shifter_down()) << "slot " << slot;
            EXPECT_EQ(nullptr, f->foe()) << "slot " << slot;
        }
    }
    tw.world().new_specials = 1;
    for (int slot = 1; slot <= 4; ++slot) {
        f->set_current_special(static_cast<char>(slot));
        EXPECT_FALSE(og::test::check_special_ai(*fd, f))
            << "slot " << slot << ": nothing to do with the setting on";
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Slot 1: a dying ally (below 30 %) that SWAP would take, with its attacker
// within 40 of it, makes the gate pick SWAP; failing that, a hurt faerie
// (below half) with a foe within 30 picks BLINK.
//
// Proof it can fail: `og.fmul(partner.max_hp, 0.3)` -> `0.0` in the staged
// kit_faerie.lua printed
//   Value of: og::test::check_special_ai(*fd, f)  Actual: false  Expected: true
//   Expected equality of these values: 1 f->shifter_down() Which is: 0
// and `self.hp < og.fdiv(self.max_hp, 2.0)` -> `self.hp < 0.0` printed
//   Value of: og::test::check_special_ai(*fd, f)  Actual: false  Expected: true
//   Expected equality of these values: 0 f->shifter_down() Which is: 1
// Without the price check (the gate before it could read SWAP's price),
// "BLINK, not an unaffordable SWAP" read shift 1 and "19 MP, not cornered:
// hold" answered true.
TEST(KitFaerie, ai_blink_fires_when_a_dying_ally_is_the_swap_partner_or_she_is_cornered)
{
    og::test::ScopedHookFailureGuard guard;
    const FamilyDescriptor* fd = faerie_fd();
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 1, 52.0f, 160, 160);
    living* ally = add_living(tw, FAMILY_SOLDIER, 1, 200, 160);  // 40
    living* attacker = add_living(tw, FAMILY_ORC, 0, 216, 160);  // 56
    ASSERT_TRUE(f && ally && attacker);
    ally->set_foe(attacker);
    set_hp(ally, 20.0f, 100.0f);
    f->set_current_special(1);

    f->set_shifter_down(0);
    EXPECT_TRUE(og::test::check_special_ai(*fd, f)) << "dying ally";
    EXPECT_EQ(1, f->shifter_down()) << "SWAP";

    set_hp(ally, 40.0f, 100.0f);
    EXPECT_FALSE(og::test::check_special_ai(*fd, f)) << "ally not dying";
    set_hp(ally, 20.0f, 100.0f);
    attacker->setxy(260, 160);  // 60 from the ally, 100 from the faerie
    EXPECT_FALSE(og::test::check_special_ai(*fd, f)) << "attacker not on it";
    attacker->set_dead(1);
    attacker->setxy(216, 160);
    EXPECT_FALSE(og::test::check_special_ai(*fd, f)) << "attacker dead";

    // Cornered: a foe within 30 and the faerie below half.
    living* close = add_living(tw, FAMILY_SOLDIER, 0, 136, 160);  // 24
    ASSERT_NE(nullptr, close);
    set_hp(f, 70.0f, 75.0f);
    EXPECT_FALSE(og::test::check_special_ai(*fd, f)) << "not hurt";
    set_hp(f, 30.0f, 75.0f);
    f->set_shifter_down(1);
    EXPECT_TRUE(og::test::check_special_ai(*fd, f)) << "cornered and hurt";
    EXPECT_EQ(0, f->shifter_down()) << "BLINK";

    // SWAP costs 20, BLINK 8: with the dying ally back in play, a faerie
    // that cannot pay for SWAP does not pick it (the engine would refuse it
    // for mana); cornered, she blinks instead, and with nothing else to do
    // she holds.
    attacker->set_dead(0);
    ally->setxy(180, 160);  // 20: nearer than the close foe (24), so the
                            // dying ally is SWAP's partner again
    f->stats()->set_magicpoints(20.0f);
    f->set_shifter_down(0);
    EXPECT_TRUE(og::test::check_special_ai(*fd, f)) << "20 MP pays for SWAP";
    EXPECT_EQ(1, f->shifter_down()) << "SWAP at 20 MP";
    f->stats()->set_magicpoints(19.0f);
    f->set_shifter_down(1);
    EXPECT_TRUE(og::test::check_special_ai(*fd, f)) << "19 MP, cornered";
    EXPECT_EQ(0, f->shifter_down()) << "BLINK, not an unaffordable SWAP";
    set_hp(f, 75.0f, 75.0f);
    f->set_shifter_down(7);
    EXPECT_FALSE(og::test::check_special_ai(*fd, f))
        << "19 MP, not cornered: hold";
    EXPECT_EQ(7, f->shifter_down()) << "no shift chosen";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The whole bot path: check_special() draws its shift coin, the gate picks
// SWAP whichever way the coin fell, and special() trades the dying ally out.
//
// Proof it can fail: `og.fmul(partner.max_hp, 0.3)` -> `0.0` in the staged
// kit_faerie.lua printed
//   Value of: f->check_special()  Actual: false  Expected: true
TEST(KitFaerie, bot_faerie_swaps_a_dying_ally_out)
{
    for (const std::uint32_t coin : {0u, 1u}) {
        og::test::ScopedHookFailureGuard guard;
        TestGameWorld tw;
        tw.world().new_specials = 1;
        living* f = add_faerie(tw, 1, 52.0f, 160, 160);
        living* ally = add_living(tw, FAMILY_SOLDIER, 1, 200, 160);
        living* attacker = add_living(tw, FAMILY_ORC, 0, 216, 160);
        ASSERT_TRUE(f && ally && attacker);
        f->set_act_type(ACT_RANDOM);
        ally->set_foe(attacker);
        set_hp(ally, 10.0f, 100.0f);
        f->set_current_special(1);
        for (std::uint32_t seed = 1;; ++seed) {
            og::sim::SimRandom probe(seed);
            if (probe.next(2) == coin) {
                tw.world().rng_.state_ = seed;
                break;
            }
        }
        ASSERT_TRUE(f->check_special()) << "coin " << coin;
        ASSERT_EQ(1, f->shifter_down()) << "coin " << coin;
        ASSERT_TRUE(f->special()) << "coin " << coin;
        EXPECT_EQ(200, f->xpos()) << "coin " << coin;
        EXPECT_EQ(160, ally->xpos()) << "coin " << coin;
        EXPECT_FLOAT_EQ(32.0f, f->stats()->magicpoints()) << "coin " << coin;
        EXPECT_EQ(0u, guard.count()) << guard.message();
    }
}

// Slot 2: GLIMMER when a foe is within 60.
//
// Proof it can fail: `og.find_foes_in_range("ob", 60, self)` -> `..., 0,
// ...` in the staged kit_faerie.lua printed
//   Value of: og::test::check_special_ai(*fd, f)  Actual: false  Expected: true
TEST(KitFaerie, ai_glimmer_fires_when_a_foe_is_within_60)
{
    og::test::ScopedHookFailureGuard guard;
    const FamilyDescriptor* fd = faerie_fd();
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 4, 100.0f, 160, 160);
    living* foe = add_living(tw, FAMILY_SOLDIER, 0, 230, 160);  // 70
    living* friend_ = add_living(tw, FAMILY_SOLDIER, 1, 170, 160);
    ASSERT_TRUE(f && foe && friend_);
    f->set_current_special(2);
    EXPECT_FALSE(og::test::check_special_ai(*fd, f)) << "foe at 70, ally near";
    foe->setxy(210, 160);  // 50
    EXPECT_TRUE(og::test::check_special_ai(*fd, f)) << "foe at 50";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Slot 3: HASTEN the ally HASTEN would take when it is fighting and not yet
// quick; else HASTE SELF when the faerie's own live foe is within 60.
//
// Proof it can fail: `ally:speed_bonus_left() == 0` -> `~= 0` in the staged
// kit_faerie.lua printed
//   Value of: og::test::check_special_ai(*fd, f)  Actual: false  Expected: true
//   Expected equality of these values: 0 f->shifter_down() Which is: 1
TEST(KitFaerie, ai_hasten_fires_for_a_fighting_ally_or_a_foe_on_her)
{
    og::test::ScopedHookFailureGuard guard;
    const FamilyDescriptor* fd = faerie_fd();
    TestGameWorld tw;
    tw.world().new_specials = 1;
    living* f = add_faerie(tw, 7, 200.0f, 160, 160);
    living* ally = add_living(tw, FAMILY_SOLDIER, 1, 184, 160);   // 24
    living* foe = add_living(tw, FAMILY_SOLDIER, 0, 300, 160);    // 140
    ASSERT_TRUE(f && ally && foe);
    f->set_current_special(3);

    EXPECT_FALSE(og::test::check_special_ai(*fd, f)) << "no one fighting";
    ally->set_foe(foe);
    f->set_shifter_down(1);
    EXPECT_TRUE(og::test::check_special_ai(*fd, f)) << "fighting ally";
    EXPECT_EQ(0, f->shifter_down()) << "HASTEN";

    ally->set_speed_bonus_left(20);
    EXPECT_FALSE(og::test::check_special_ai(*fd, f)) << "ally already quick";
    f->set_foe(foe);
    EXPECT_FALSE(og::test::check_special_ai(*fd, f)) << "her foe is 140 away";
    foe->setxy(210, 160);  // 50
    EXPECT_TRUE(og::test::check_special_ai(*fd, f)) << "her foe is 50 away";
    EXPECT_EQ(1, f->shifter_down()) << "HASTE SELF";
    foe->set_dead(1);
    EXPECT_FALSE(og::test::check_special_ai(*fd, f)) << "her foe is dead";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Slot 4: WISH only with someone to raise AND two allies nearby below half.
//
// Proof it can fail: `>= 2` -> `>= 3` in the staged kit_faerie.lua printed
//   Value of: og::test::check_special_ai(*fd, s.faerie)  Actual: false  Expected: true
TEST(KitFaerie, ai_wish_fires_with_a_fallen_ally_and_two_wounded)
{
    og::test::ScopedHookFailureGuard guard;
    const FamilyDescriptor* fd = faerie_fd();
    WishStage s(1);
    ASSERT_TRUE(s.ready());
    s.faerie->set_current_special(4);
    EXPECT_TRUE(og::test::check_special_ai(*fd, s.faerie));

    set_hp(s.hurt_b, 50.0f, 80.0f);  // not below half any more
    EXPECT_FALSE(og::test::check_special_ai(*fd, s.faerie)) << "one wounded";
    set_hp(s.hurt_b, 10.0f, 80.0f);
    s.near_stain->set_dead(1);
    s.far_stain->set_dead(1);
    EXPECT_FALSE(og::test::check_special_ai(*fd, s.faerie)) << "no one to raise";
    EXPECT_FALSE(s.faerie->dead()) << "a gate never casts";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

namespace {

struct Skirmish {
    int flashes = 0;       // blink, swap and wish flashes seen
    int hastened = 0;      // walker-ticks with a speed bonus running
    int bursts = 0;        // ticks on which eight or more sprinkles appeared
    int raised = 0;        // livings that appeared during the fight
    bool faerie_alive = true;
    std::string timeline;  // one line per tick on which something kit-like happened
};

// A small fight run by the sim's own tick: a level-10 bot faerie (team 1),
// already hurt, with two soldiers, one of them badly hurt, against three
// orcs, one of them on her; a fallen soldier's stain lies beside her.
// `ticks` ticks from a fixed world seed.
Skirmish run_skirmish(short new_specials, int ticks)
{
    TestGameWorld tw;
    tw.world().new_specials = new_specials;
    tw.world().rng_.state_ = 12345u;
    Skirmish out;
    living* f = add_faerie(tw, 10, 300.0f, 160, 160);
    living* hurt = add_living(tw, FAMILY_SOLDIER, 1, 200, 160);
    living* mate = add_living(tw, FAMILY_SOLDIER, 1, 160, 200);
    living* orc_a = add_living(tw, FAMILY_ORC, 0, 222, 160);
    living* orc_b = add_living(tw, FAMILY_ORC, 0, 200, 222);
    living* orc_c = add_living(tw, FAMILY_ORC, 0, 176, 176);
    walker* stain = add_stain(tw, 1, 180, 140, FAMILY_SOLDIER, 4, 110.0f);
    if (!(f && hurt && mate && orc_a && orc_b && orc_c && stain))
        return out;
    for (living* w : {f, hurt, mate, orc_a, orc_b, orc_c})
        w->set_act_type(ACT_RANDOM);
    set_hp(hurt, 20.0f, 100.0f);
    set_hp(mate, 45.0f, 100.0f);
    set_hp(f, 30.0f, 75.0f);
    hurt->set_foe(orc_a);
    mate->set_foe(orc_b);
    orc_c->set_foe(f);
    std::size_t livings = 0;
    for (const auto& uptr : tw.world().oblist)
        if (uptr && uptr->query_order() == Order::Living)
            ++livings;
    int sprinkles_before = sprinkles(tw);
    for (int tick = 0; tick < ticks; ++tick) {
        const int flashes_before = flashes(tw);
        tw.world().tick();
        std::string line;
        const int new_flashes = flashes(tw) - flashes_before;
        if (new_flashes > 0) {
            out.flashes += new_flashes;
            line += " flash x" + std::to_string(new_flashes);
        }
        for (const auto& uptr : tw.world().oblist)
            if (uptr && !uptr->dead() && uptr->query_order() == Order::Living &&
                uptr->speed_bonus_left() > 0)
                ++out.hastened;
        const int sprinkles_now = sprinkles(tw);
        if (sprinkles_now - sprinkles_before >= 8) {
            ++out.bursts;
            line += " glimmer";
        }
        sprinkles_before = sprinkles_now;
        std::size_t now = 0;
        for (const auto& uptr : tw.world().oblist)
            if (uptr && uptr->query_order() == Order::Living)
                ++now;
        if (now > livings) {
            out.raised += static_cast<int>(now - livings);
            line += " raised";
        }
        livings = now;
        if (f->dead() && out.faerie_alive) {
            out.faerie_alive = false;
            line += " faerie gone";
        }
        if (!line.empty())
            out.timeline += "tick " + std::to_string(tick) + ":" + line +
                            " (faerie " + std::to_string(f->xpos()) + "," +
                            std::to_string(f->ypos()) + " mp " +
                            std::to_string(static_cast<int>(f->stats()->magicpoints())) +
                            ")\n";
    }
    return out;
}

}  // namespace

// The kit in a running fight: with the setting on the bot faerie uses it
// (a blink, swap or wish flash, a haste, a glimmer burst or a raised ally
// appears); with the setting off she is the classic faerie and none of
// those ever happen. OG_FAERIE_SKIRMISH_LOG=1 prints both timelines. (Seen
// when this was written: on, the orc's hits make her blink away at tick 56
// and she lasts to tick 206; off, she has no special and falls at tick 69.)
//
// Proof it can fail: `self.hp < og.fdiv(self.max_hp, 2.0)` -> `self.hp <
// 0.0` in the staged kit_faerie.lua (she never feels cornered) printed
//   Expected: (on.flashes + on.hastened + on.bursts + on.raised) > (0),
//   actual: 0 vs 0
TEST(KitFaerie, bot_faerie_uses_her_kit_in_a_running_fight_only_when_on)
{
    og::test::ScopedHookFailureGuard guard;
    const Skirmish on = run_skirmish(1, 400);
    const Skirmish off = run_skirmish(0, 400);
    if (std::getenv("OG_FAERIE_SKIRMISH_LOG") != nullptr)
        std::printf("--- setting on\n%s--- setting off\n%s", on.timeline.c_str(),
                    off.timeline.c_str());
    EXPECT_GT(on.flashes + on.hastened + on.bursts + on.raised, 0)
        << "with the setting on the bot faerie casts";
    EXPECT_EQ(0, off.flashes);
    EXPECT_EQ(0, off.hastened);
    EXPECT_EQ(0, off.bursts);
    EXPECT_EQ(0, off.raised);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
