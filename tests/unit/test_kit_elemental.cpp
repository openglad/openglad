/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Specials, fire elemental: IMMOLATE, METEOR RAIN, REKINDLE /
// SUPERNOVA, the slot-1 parting shot, and the bot gates.
//
// Every cast goes through the real walker::special() (the cost gate and
// the charge included) against the shipped core pack, in a hand-built
// world whose New Specials flag the test sets itself. Each ON case has an
// OFF twin with the same staging: the new slot is not in play, special()
// fails with NoMP, nothing is spawned and no mana moves.
//
// Teeth: each case names, in its comment, the staged-copy perturbation
// (an edit under build/ci-test/packs/core, then the test binary rerun)
// that was run once and turned it red.

#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_gameplay_context_scope.h"

#include <openglad/core/constants.h>
#include <openglad/core/family_presentation.h>
#include <openglad/gameplay/families/effect_family_descriptor.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/families/specials_view.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/sim_event_log.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/level_runtime_data.h>
#include <openglad/resources/gparser.h>
#include <openglad/resources/save_data.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using Failure = walker::SpecialFailure;

struct ElementalWorld {
    LevelRuntimeData level{1, true};
    SaveData save;
    og::sim::SimEventLog events;
    ScopedGameplayContext gameplay;

    explicit ElementalWorld(short new_specials)
        : gameplay(level, save, events, cfg)
    {
        level.create_new_grid();
        save.allied_mode = 0;
        level.world().allied_mode = 0;
        level.set_sim_context(&save, &events, &cfg);
        level.world().new_specials = new_specials;
        level.world().tick_count_ = 1000;
    }

    GameWorld& world() { return level.world(); }

    ElementalWorld(const ElementalWorld&) = delete;
    ElementalWorld& operator=(const ElementalWorld&) = delete;
};

void place(walker* w, int x, int y)
{
    w->setxy(static_cast<short>(x), static_cast<short>(y));
}

living* add_living(ElementalWorld& w, int family, unsigned char team,
                   int x, int y, short level = 4)
{
    walker* ob = w.world().add_ob(Order::Living, family);
    if (ob == nullptr)
        return nullptr;
    ob->set_team_num(team);
    ob->set_real_team_num(255);
    place(ob, x, y);
    ob->stats()->set_level(level);
    ob->stats()->set_armor(0.0f);
    ob->stats()->set_max_hitpoints(100.0f);
    ob->stats()->set_hitpoints(100.0f);
    // No mana: a struck bot casts nothing of its own (stats.cpp's hit
    // path rolls check_special() for every hit bot).
    ob->stats()->set_magicpoints(0.0f);
    return dynamic_cast<living*>(ob);
}

// A level-`level` elemental on team 0 at (100, 100) with `mp` mana,
// parked on `slot`.
living* add_elemental(ElementalWorld& w, short level, float mp, int slot)
{
    living* e = add_living(w, FAMILY_FIREELEMENTAL, 0, 100, 100, level);
    if (e == nullptr)
        return nullptr;
    e->stats()->set_max_magicpoints(mp);
    e->stats()->set_magicpoints(mp);
    e->set_current_special(static_cast<char>(slot));
    e->set_shifter_down(0);
    return e;
}

int fx_family(const char* id)
{
    return og::families::resolve_family_string_id(Order::FX, id);
}

std::vector<walker*> live_of(ElementalWorld& w, Order order, int family)
{
    std::vector<walker*> found;
    const auto scan = [&](const auto& list) {
        for (const auto& p : list)
            if (p && !p->dead() && p->query_order() == order &&
                static_cast<int>(static_cast<unsigned char>(p->family())) ==
                    family)
                found.push_back(p.get());
    };
    scan(w.world().oblist);
    scan(w.world().weaplist);
    scan(w.world().fxlist);
    return found;
}

std::vector<walker*> markers(ElementalWorld& w)
{
    return live_of(w, Order::FX, fx_family("core:kit_marker"));
}

std::vector<walker*> embers(ElementalWorld& w)
{
    return live_of(w, Order::FX, fx_family("core:ember"));
}

std::vector<walker*> explosions(ElementalWorld& w)
{
    return live_of(w, Order::FX, FAMILY_EXPLOSION);
}

std::vector<walker*> meteors(ElementalWorld& w)
{
    return live_of(w, Order::Weapon, FAMILY_METEOR);
}

bool channelling(const walker& w)
{
    return (w.kit_state() & KIT_CHANNEL) != 0;
}

struct Cast {
    bool ok = false;
    Failure why = Failure::None;
    std::string reason;
};

Cast cast(walker* w)
{
    Cast c;
    c.ok = w->special(&c.why, &c.reason);
    return c;
}

float hp(const walker* w) { return w->stats()->hitpoints(); }

// One hit of nominal damage `d` lands as d - sqrt(d)/2 + rng(floor(sqrt
// d)), rounded to hit points (combat_math.cpp): the band a single hit
// of `d` can take, so a test can tell a doubled burn from a plain one.
struct Band {
    int lo;
    int hi;
};
Band hit_band(float d)
{
    const float root = std::sqrt(d);
    const float low = d - root / 2.0f;
    const float high = low + std::floor(root) - 1.0f;
    return {static_cast<int>(std::floor(low + 0.5f)),
            static_cast<int>(std::floor(high + 0.5f))};
}

::testing::AssertionResult lost_one_hit_of(float d, float before, float after)
{
    const Band b = hit_band(d);
    const float lost = before - after;
    if (lost >= static_cast<float>(b.lo) && lost <= static_cast<float>(b.hi))
        return ::testing::AssertionSuccess();
    return ::testing::AssertionFailure()
           << "lost " << lost << ", one hit of " << d << " takes " << b.lo
           << ".." << b.hi;
}
float mp(const walker* w) { return w->stats()->magicpoints(); }

int centre_x(const walker* w) { return w->xpos() + w->sizex() / 2; }
int centre_y(const walker* w) { return w->ypos() + w->sizey() / 2; }

// The OFF twin every new slot shares: with the setting off the slot is not
// in play, so the cost gate refuses (NoMP) before any script runs, and no
// kit entity appears. Perturbation (staged living-06: `new_kit = true`
// dropped from the slot's row): the slot casts with the setting off -> red,
// for each of the four twins.
void expect_off_slot_refused(int slot, short shift)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(0);
    living* e = add_elemental(w, 10, 1000.0f, slot);
    ASSERT_NE(nullptr, e);
    e->stats()->set_hitpoints(50.0f);
    e->set_shifter_down(shift);
    living* foe = add_living(w, FAMILY_SOLDIER, 1, 130, 100);
    ASSERT_NE(nullptr, foe);
    e->set_foe(foe);
    const Cast c = cast(e);
    EXPECT_FALSE(c.ok) << "slot " << slot;
    EXPECT_EQ(Failure::NoMP, c.why) << "slot " << slot;
    EXPECT_FLOAT_EQ(1000.0f, mp(e)) << "slot " << slot;
    EXPECT_FLOAT_EQ(50.0f, hp(e)) << "slot " << slot;
    EXPECT_FALSE(e->dead()) << "slot " << slot;
    EXPECT_FALSE(channelling(*e)) << "slot " << slot;
    EXPECT_TRUE(markers(w).empty()) << "slot " << slot;
    EXPECT_TRUE(explosions(w).empty()) << "slot " << slot;
    EXPECT_TRUE(live_of(w, Order::FX, FAMILY_HIT).empty()) << "slot " << slot;
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

}  // namespace

// ---------------------------------------------------------------------------
// The table as each setting sees it.
// ---------------------------------------------------------------------------

// Perturbation (staged living-06: name = "METEORS"): red.
TEST(KitElemental, specials_table_reads_the_kit_on_and_the_classic_slot_off)
{
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_FIREELEMENTAL);
    ASSERT_NE(nullptr, fd);
    const char* on[] = {"STARBURST", "IMMOLATE", "METEOR RAIN", "REKINDLE"};
    for (int slot = 1; slot <= 4; ++slot) {
        EXPECT_STREQ(on[slot - 1], og::sim::special_name(fd, slot, 1));
        EXPECT_STREQ(slot == 1 ? "STARBURST" : "NONE",
                     og::sim::special_name(fd, slot, 0));
    }
    EXPECT_STREQ("SUPERNOVA", og::sim::alternate_name(fd, 4, 1));
    EXPECT_STREQ("NONE", og::sim::alternate_name(fd, 4, 0));
    EXPECT_EQ(10, og::sim::alternate_cost_in_play(fd, 4, 1));
    EXPECT_EQ(0, og::sim::alternate_cost_in_play(fd, 4, 0));

    ElementalWorld w(1);
    living* e = add_elemental(w, 10, 0.0f, 1);
    ASSERT_NE(nullptr, e);
    EXPECT_EQ(50, og::sim::cast_cost(*e, 1, false));
    EXPECT_EQ(30, og::sim::cast_cost(*e, 2, false));
    EXPECT_EQ(70, og::sim::cast_cost(*e, 3, false));
    EXPECT_EQ(40, og::sim::cast_cost(*e, 4, false));
    EXPECT_EQ(10, og::sim::cast_cost(*e, 4, true));
    w.world().new_specials = 0;
    EXPECT_EQ(50, og::sim::cast_cost(*e, 1, false));
    for (int slot = 2; slot <= 4; ++slot)
        EXPECT_EQ(kSpecialCostDisabled,
                  og::sim::cast_cost(*e, slot, true));
}

// ---------------------------------------------------------------------------
// IMMOLATE
// ---------------------------------------------------------------------------

// Lit at level 4 with 100 MP: 30 to light, then 2 a tick. A foe that struck
// the elemental and stands touching it burns 2 x (6 + 4) a tick; a foe that
// only touches it burns 6 + 4; a far foe and a touching ally do not burn.
// An ember drops at the elemental's feet every fourth tick.
// The touching foes stand where a touch really happens, not only straight
// beside the elemental: the striker half a body lower on its right, one
// foe touching its top-left corner diagonally, and a giant skeleton whose
// big box reaches over it from the top-left. The far foe sits well inside
// the finder's wide search radius, so only the box test spares it.
// Perturbation (staged kit tuning immolate_drain = 200): the first marker
// tick finds 70 < 200 MP and puts the fire out; nothing burns -> red.
// Perturbation (staged kit: the contact search radius back to
// owner:sizex() + 4, which measures top-left corner to top-left corner):
// the lower striker, the corner foe and the giant are not burned -> red.
TEST(KitElemental, immolate_drains_two_per_tick_burns_contact_melee_most_and_lays_embers)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 4, 100.0f, 2);
    ASSERT_NE(nullptr, e);
    const int ex = e->xpos();
    const int ey = e->ypos();
    living* striker = add_living(w, FAMILY_SOLDIER, 1, ex + e->sizex(), ey + 10);
    living* toucher = add_living(w, FAMILY_SOLDIER, 1, ex, ey + e->sizey() + 2);
    living* far = add_living(w, FAMILY_SOLDIER, 1, ex + 120, ey);
    living* ally = add_living(w, FAMILY_SOLDIER, 0, ex - e->sizex(), ey);
    ASSERT_TRUE(striker && toucher && far && ally);
    living* corner = add_living(w, FAMILY_SOLDIER, 1, 0, 0);
    ASSERT_NE(nullptr, corner);
    place(corner, ex - corner->sizex() - 2, ey - corner->sizey() - 2);
    living* giant = add_living(w, FAMILY_GIANT_SKELETON, 1, 0, 0);
    ASSERT_NE(nullptr, giant);
    ASSERT_GE(giant->sizex(), 48) << "the giant's box is the big one";
    place(giant, ex - giant->sizex() + 2, ey - giant->sizey() + 2);
    // The elemental is a bot: struck with mana it may light the fire
    // itself (the hit path), so it is struck dry and filled after.
    e->stats()->set_magicpoints(0.0f);
    ASSERT_TRUE(striker->attack(e)) << "the striker hits the elemental first";
    e->stats()->set_magicpoints(100.0f);
    e->set_current_special(2);
    ASSERT_EQ(striker->entity_id(), e->last_attacker_id());

    const Cast lit = cast(e);
    ASSERT_TRUE(lit.ok) << lit.reason;
    EXPECT_FLOAT_EQ(70.0f, mp(e));
    EXPECT_TRUE(channelling(*e));
    const auto ms = markers(w);
    ASSERT_EQ(1u, ms.size());
    walker* marker = ms[0];
    EXPECT_EQ(e, marker->owner());

    const float striker0 = hp(striker);
    const float toucher0 = hp(toucher);
    ASSERT_TRUE(marker->act());
    EXPECT_FLOAT_EQ(68.0f, mp(e)) << "two mana a tick";
    // The two bands (18..21 and 8..10) do not overlap: a doubled burn is
    // told from a plain one whatever the damage roll.
    EXPECT_TRUE(lost_one_hit_of(20.0f, striker0, hp(striker)))
        << "the melee attacker burns double";
    EXPECT_TRUE(lost_one_hit_of(10.0f, toucher0, hp(toucher)))
        << "contact burns 6 + level";
    EXPECT_TRUE(lost_one_hit_of(10.0f, 100.0f, hp(corner)))
        << "a diagonal touch burns too";
    EXPECT_TRUE(lost_one_hit_of(10.0f, 100.0f, hp(giant)))
        << "a big foe reaching over the elemental burns";
    EXPECT_FLOAT_EQ(100.0f, hp(far));
    EXPECT_FLOAT_EQ(100.0f, hp(ally));
    EXPECT_TRUE(embers(w).empty()) << "no ember on the first tick";

    for (int tick = 2; tick <= 4; ++tick)
        ASSERT_TRUE(marker->act());
    EXPECT_FLOAT_EQ(62.0f, mp(e));
    const auto es = embers(w);
    ASSERT_EQ(1u, es.size()) << "an ember on the fourth tick";
    walker* ember = es[0];
    EXPECT_EQ(e, ember->owner());
    EXPECT_EQ(e->team_num(), ember->team_num());
    EXPECT_EQ(40, ember->lifetime());
    EXPECT_FLOAT_EQ(6.0f, ember->damage()) << "4 + level / 2";
    EXPECT_EQ(e->ypos() + e->sizey(), ember->ypos() + ember->sizey())
        << "at the elemental's feet";
    EXPECT_EQ(e->xpos() + (e->sizex() - ember->sizex()) / 2, ember->xpos())
        << "centred under the elemental";
    for (int tick = 5; tick <= 8; ++tick)
        ASSERT_TRUE(marker->act());
    EXPECT_EQ(2u, embers(w).size());
    EXPECT_FALSE(marker->dead());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitElemental, immolate_is_not_cast_with_new_specials_off)
{
    expect_off_slot_refused(2, 0);
}

// An ember burns the foes standing on it every sixth tick of its forty
// (36, 30, 24, 18, 12, 6 left: six burns of 4 + level / 2), never an ally,
// shows its dying frame for its last ten ticks and dies at zero. "Standing
// on it" means any overlap: here the ember lies under the middle of each
// foe, not under its top-left corner.
// Perturbation (staged ember tuning ember_pulse = 999): no burn -> red.
// Perturbation (staged ember lib: the search radius back to the ember's
// own width, which measures top-left corner to top-left corner): neither
// the crosser nor the giant burns -> red.
TEST(KitElemental, embers_burn_crossers_then_die)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 4, 1000.0f, 2);
    ASSERT_NE(nullptr, e);
    ASSERT_TRUE(cast(e).ok);
    walker* marker = markers(w).at(0);
    for (int tick = 1; tick <= 4; ++tick)
        ASSERT_TRUE(marker->act());
    ASSERT_EQ(1u, embers(w).size());
    walker* ember = embers(w)[0];
    // The elemental walks off; a foe and an ally step onto the ember, the
    // foe with the ember right under the middle of its body, and a giant
    // skeleton stands with the ember under its middle too.
    place(e, e->xpos() + 150, e->ypos());
    living* crosser = add_living(w, FAMILY_SOLDIER, 1, 0, 0);
    living* friend_on_it = add_living(w, FAMILY_SOLDIER, 0, ember->xpos(), ember->ypos());
    living* giant = add_living(w, FAMILY_GIANT_SKELETON, 1, 0, 0);
    ASSERT_TRUE(crosser && friend_on_it && giant);
    place(crosser, centre_x(ember) - crosser->sizex() / 2,
          centre_y(ember) - crosser->sizey() / 2);
    place(giant, centre_x(ember) - giant->sizex() / 2,
          centre_y(ember) - giant->sizey() / 2);
    EXPECT_EQ(0, ember->frame());

    int acts = 0;
    while (ember->lifetime() > 36) {
        ASSERT_TRUE(ember->act());
        ++acts;
    }
    EXPECT_EQ(4, acts);
    EXPECT_TRUE(lost_one_hit_of(6.0f, 100.0f, hp(crosser)))
        << "one burn at 36 ticks left";
    EXPECT_TRUE(lost_one_hit_of(6.0f, 100.0f, hp(giant)))
        << "the giant over the ember burns too";
    while (!ember->dead()) {
        ASSERT_TRUE(ember->act());
        if (!ember->dead() && ember->lifetime() == 9) {
            EXPECT_EQ(1, ember->frame()) << "the dying frame";
        }
        ++acts;
        ASSERT_LE(acts, 40);
    }
    EXPECT_EQ(40, acts) << "an ember lasts its lifetime";
    const Band burn = hit_band(6.0f);
    EXPECT_GE(100.0f - hp(crosser), static_cast<float>(6 * burn.lo))
        << "six burns of six";
    EXPECT_LE(100.0f - hp(crosser), static_cast<float>(6 * burn.hi))
        << "six burns of six";
    EXPECT_FLOAT_EQ(100.0f, hp(friend_on_it));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The burn scans search a radius wide enough for any foe box up to 64 x 64
// to overlap the burning box (effect_ember.lua, LIVING_BOX): a bigger
// living would be missed at some overlaps. Every core living fits.
// Perturbation (a core living box over 64 px, e.g. the bound lowered here
// to 32): the golem and the giant skeleton exceed it -> red.
TEST(KitElemental, every_core_living_box_fits_the_burn_search)
{
    constexpr int kLivingBox = 64;
    ElementalWorld w(1);
    for (int family = 0; family < NUM_FAMILIES; ++family) {
        living* l = add_living(w, family, 1, 100, 100);
        ASSERT_NE(nullptr, l) << "family " << family;
        EXPECT_LE(l->sizex(), kLivingBox) << "family " << family;
        EXPECT_LE(l->sizey(), kLivingBox) << "family " << family;
    }
}

// 31 MP: lighting takes 30, the first tick finds 1 < 2 and the fire goes
// out: the channel ends, the marker dies, nothing is drained or dropped.
// Perturbation (staged kit tuning immolate_drain = 0): the fire keeps
// burning on 1 MP -> red.
TEST(KitElemental, immolate_at_low_mana_goes_out)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 4, 31.0f, 2);
    ASSERT_NE(nullptr, e);
    ASSERT_TRUE(cast(e).ok);
    EXPECT_FLOAT_EQ(1.0f, mp(e));
    EXPECT_TRUE(channelling(*e));
    walker* marker = markers(w).at(0);
    ASSERT_TRUE(marker->act());
    EXPECT_TRUE(marker->dead()) << "burnt out";
    EXPECT_FALSE(channelling(*e)) << "the channel ends with the fire";
    EXPECT_FLOAT_EQ(1.0f, mp(e));
    EXPECT_TRUE(embers(w).empty());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// With mana to spare the fire lasts immolate_max (200) ticks and no more.
// Perturbation (staged kit tuning immolate_max = 5): out on tick 5 -> red.
TEST(KitElemental, immolate_burns_out_after_its_longest_burn)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 4, 10000.0f, 2);
    ASSERT_NE(nullptr, e);
    ASSERT_TRUE(cast(e).ok);
    walker* marker = markers(w).at(0);
    for (int tick = 1; tick < 200; ++tick) {
        ASSERT_TRUE(marker->act());
        ASSERT_FALSE(marker->dead()) << "tick " << tick;
    }
    ASSERT_TRUE(marker->act());
    EXPECT_TRUE(marker->dead()) << "out after 200 ticks";
    EXPECT_FALSE(channelling(*e));
    EXPECT_FLOAT_EQ(10000.0f - 30.0f - 199.0f * 2.0f, mp(e));
    EXPECT_EQ(49u, embers(w).size()) << "an ember every fourth tick";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// MP 31 -> light -> 1 MP. The next nine presses (one a tick, as a held key
// repeats) are refused silently by the latch and spend nothing; the press
// on the tenth tick after lighting quenches it at no cost, at 1 MP.
// Perturbation (staged kit tuning kit_latch = 0): the first repeat
// quenches -> red.
TEST(KitElemental, second_press_ends_it_free_after_the_latch)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 4, 31.0f, 2);
    ASSERT_NE(nullptr, e);
    const std::uint32_t lit_tick = w.world().tick_count_;
    ASSERT_TRUE(cast(e).ok);
    ASSERT_FLOAT_EQ(1.0f, mp(e));
    for (std::uint32_t t = 1; t <= 9; ++t) {
        w.world().tick_count_ = lit_tick + t;
        const Cast held = cast(e);
        EXPECT_FALSE(held.ok) << "tick +" << t;
        EXPECT_EQ(Failure::ScriptDeclined, held.why) << "tick +" << t;
        EXPECT_EQ("IMMOLATE SETTLING", held.reason) << "tick +" << t;
        EXPECT_TRUE(channelling(*e)) << "tick +" << t;
        EXPECT_FLOAT_EQ(1.0f, mp(e)) << "tick +" << t;
    }
    w.world().tick_count_ = lit_tick + 10;
    const Cast quench = cast(e);
    EXPECT_TRUE(quench.ok) << quench.reason;
    EXPECT_FALSE(channelling(*e));
    EXPECT_TRUE(markers(w).empty()) << "the marker died with the quench";
    EXPECT_FLOAT_EQ(1.0f, mp(e)) << "quenching is free";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The channel prices every slot at 0, so while the fire burns the other
// specials refuse instead of casting for nothing; quenched, they cast and
// pay again. Perturbation (staged kit: burning() answers false): STARBURST
// casts for free while burning -> red.
TEST(KitElemental, a_burning_elemental_casts_nothing_else_for_free)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 10, 1000.0f, 2);
    ASSERT_NE(nullptr, e);
    e->stats()->set_hitpoints(50.0f);
    living* foe = add_living(w, FAMILY_SOLDIER, 1, 200, 100);
    ASSERT_NE(nullptr, foe);
    e->set_foe(foe);
    ASSERT_TRUE(cast(e).ok);
    ASSERT_FLOAT_EQ(970.0f, mp(e));
    for (int slot = 1; slot <= 4; slot += (slot == 1 ? 2 : 1)) {
        for (short shift = 0; shift <= 1; ++shift) {
            e->set_current_special(static_cast<char>(slot));
            e->set_shifter_down(shift);
            const Cast c = cast(e);
            EXPECT_FALSE(c.ok) << "slot " << slot << " shift " << shift;
            EXPECT_EQ("QUENCH IMMOLATE FIRST", c.reason)
                << "slot " << slot << " shift " << shift;
        }
    }
    EXPECT_FLOAT_EQ(970.0f, mp(e));
    EXPECT_FLOAT_EQ(50.0f, hp(e));
    EXPECT_FALSE(e->dead());
    EXPECT_TRUE(meteors(w).empty());
    EXPECT_EQ(1u, markers(w).size()) << "no rain marker, only the fire";

    // Quenched (after the latch), STARBURST casts and pays its 50.
    e->set_current_special(2);
    e->set_shifter_down(0);
    w.world().tick_count_ += 10;
    ASSERT_TRUE(cast(e).ok);
    e->set_current_special(1);
    ASSERT_TRUE(cast(e).ok);
    EXPECT_FLOAT_EQ(920.0f, mp(e));
    EXPECT_EQ(8u, meteors(w).size());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// Killed while the fire burns, the elemental still fires its parting
// starburst (on_death ends the channel first) and the orphaned marker dies
// on its next tick. Perturbation (staged living-06: the on_death
// ke.end_channel(self) line removed): the parting shot is refused -> red.
TEST(KitElemental, dying_while_immolating_still_fires_the_parting_starburst)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 4, 1000.0f, 2);
    ASSERT_NE(nullptr, e);
    ASSERT_TRUE(cast(e).ok);
    walker* marker = markers(w).at(0);
    e->stats()->set_hitpoints(-5.0f);
    e->set_dead(1);
    e->death();
    EXPECT_EQ(8u, meteors(w).size()) << "the parting starburst";
    EXPECT_FALSE(channelling(*e));
    EXPECT_EQ(2, e->current_special()) << "the parked slot is restored";
    ASSERT_TRUE(marker->act());
    EXPECT_TRUE(marker->dead()) << "an ownerless fire goes out";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// METEOR RAIN
// ---------------------------------------------------------------------------

// Level 7 on a foe 100 px away: 70 MP; the rain marker sits on the foe and
// a fire explosion lands every fifth tick of sixty (twelve strikes), each
// within 40 px of the spot, each the caster's own magical blast of
// 10 + 2 x level. A foe out of range, or on another floor, is no target.
// Perturbation (staged kit tuning rain_cadence = 999): one strike, at the
// end -> red.
TEST(KitElemental, meteor_rain_strikes_the_target_area_every_five_ticks)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 7, 1000.0f, 3);
    ASSERT_NE(nullptr, e);
    living* foe = add_living(w, FAMILY_SOLDIER, 1, 200, 100);
    ASSERT_NE(nullptr, foe);
    e->set_foe(foe);
    const Cast c = cast(e);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(930.0f, mp(e));
    const auto ms = markers(w);
    ASSERT_EQ(1u, ms.size());
    walker* rain = ms[0];
    EXPECT_EQ(centre_x(foe), centre_x(rain));
    EXPECT_EQ(centre_y(foe), centre_y(rain));
    EXPECT_EQ(60, rain->lifetime());

    std::vector<std::size_t> after_tick;
    for (int tick = 1; tick <= 60; ++tick) {
        ASSERT_FALSE(rain->dead()) << "tick " << tick;
        ASSERT_TRUE(rain->act());
        after_tick.push_back(explosions(w).size());
    }
    EXPECT_TRUE(rain->dead()) << "the rain ends after sixty ticks";
    EXPECT_EQ(0u, after_tick[3]) << "nothing before the fifth tick";
    EXPECT_EQ(1u, after_tick[4]);
    EXPECT_EQ(1u, after_tick[8]);
    EXPECT_EQ(2u, after_tick[9]);
    const auto strikes = explosions(w);
    ASSERT_EQ(12u, strikes.size()) << "a strike every fifth tick of sixty";
    for (walker* s : strikes) {
        EXPECT_EQ(e, s->owner());
        EXPECT_EQ(e->team_num(), s->team_num());
        EXPECT_EQ(7, s->stats()->level());
        EXPECT_FLOAT_EQ(24.0f, s->damage());
        EXPECT_EQ(ANI_EXPLODE, s->ani_type());
        EXPECT_TRUE(s->stats()->query_bit_flags(BIT_FIRE));
        EXPECT_EQ(100, s->skip_exit()) << "the caster's own magical blast";
        EXPECT_LE(std::abs(centre_x(s) - centre_x(foe)), 40);
        EXPECT_LE(std::abs(centre_y(s) - centre_y(foe)), 40);
    }

    // Out of range (300 px): refused, nothing spent.
    place(foe, 400, 100);
    const Cast far = cast(e);
    EXPECT_FALSE(far.ok);
    EXPECT_EQ("NO TARGET IN RANGE", far.reason);
    EXPECT_FLOAT_EQ(930.0f, mp(e));
    // In range but on another floor: no target either.
    place(foe, 200, 100);
    foe->set_floor(1);
    const Cast upstairs = cast(e);
    EXPECT_FALSE(upstairs.ok);
    EXPECT_EQ("NO TARGET IN RANGE", upstairs.reason);
    EXPECT_TRUE(markers(w).empty());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// A current foe that has just died is no target: the rain falls on the
// nearest living foe instead, and with only the corpse in range it is
// refused and costs nothing.
// Perturbation (staged kit: rain_target's dead-foe check removed): the
// rain is centred on the corpse -> red.
TEST(KitElemental, meteor_rain_passes_over_a_dead_foe)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 7, 1000.0f, 3);
    ASSERT_NE(nullptr, e);
    living* corpse = add_living(w, FAMILY_SOLDIER, 1, 180, 100);
    living* foe = add_living(w, FAMILY_SOLDIER, 1, 100, 200);
    ASSERT_TRUE(corpse && foe);
    e->set_foe(corpse);
    corpse->set_dead(1);
    const Cast c = cast(e);
    ASSERT_TRUE(c.ok) << c.reason;
    EXPECT_FLOAT_EQ(930.0f, mp(e));
    walker* rain = markers(w).at(0);
    EXPECT_EQ(centre_x(foe), centre_x(rain)) << "on the living foe";
    EXPECT_EQ(centre_y(foe), centre_y(rain)) << "on the living foe";

    // The living foe falls too; the corpse is still the current foe.
    foe->set_dead(1);
    e->set_foe(corpse);
    const Cast refused = cast(e);
    EXPECT_FALSE(refused.ok);
    EXPECT_EQ("NO TARGET IN RANGE", refused.reason);
    EXPECT_FLOAT_EQ(930.0f, mp(e));
    EXPECT_EQ(1u, markers(w).size()) << "no second rain";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// With no current foe the nearest one is the target; when the caster dies
// the rain stops at once. Perturbation (staged kit: the find_near_foe
// fallback replaced by nil): no target -> red.
TEST(KitElemental, meteor_rain_finds_the_nearest_foe_and_stops_with_its_caster)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 7, 1000.0f, 3);
    ASSERT_NE(nullptr, e);
    living* foe = add_living(w, FAMILY_SOLDIER, 1, 160, 100);
    ASSERT_NE(nullptr, foe);
    ASSERT_EQ(nullptr, e->foe());
    ASSERT_TRUE(cast(e).ok);
    walker* rain = markers(w).at(0);
    EXPECT_EQ(centre_x(foe), centre_x(rain));
    e->set_dead(1);
    ASSERT_TRUE(rain->act());
    EXPECT_TRUE(rain->dead());
    EXPECT_TRUE(explosions(w).empty());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitElemental, meteor_rain_is_not_cast_with_new_specials_off)
{
    expect_off_slot_refused(3, 0);
}

// ---------------------------------------------------------------------------
// REKINDLE / SUPERNOVA
// ---------------------------------------------------------------------------

// Level 10, 10 of 100 HP: 40 MP buys 40 + 4 x 10 = 80 HP and a flash; at
// full health it refuses and spends nothing; the heal never passes max.
// Perturbation (staged kit tuning rekindle_hp = 0): 50 HP -> red.
TEST(KitElemental, rekindle_heals_and_refuses_at_full)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 10, 200.0f, 4);
    ASSERT_NE(nullptr, e);
    e->stats()->set_hitpoints(10.0f);
    ASSERT_TRUE(cast(e).ok);
    EXPECT_FLOAT_EQ(90.0f, hp(e));
    EXPECT_FLOAT_EQ(160.0f, mp(e));
    EXPECT_EQ(1u, live_of(w, Order::FX, FAMILY_HIT).size()) << "the flash";
    ASSERT_TRUE(cast(e).ok);
    EXPECT_FLOAT_EQ(100.0f, hp(e)) << "never above max";
    EXPECT_FLOAT_EQ(120.0f, mp(e));
    const Cast full = cast(e);
    EXPECT_FALSE(full.ok);
    EXPECT_EQ("ALREADY AT FULL HEALTH", full.reason);
    EXPECT_FLOAT_EQ(120.0f, mp(e));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitElemental, rekindle_is_not_cast_with_new_specials_off)
{
    expect_off_slot_refused(4, 0);
}

// Shift + slot 4 at level 5 with 60 HP: 10 MP; one explosion of level
// min(2 x 5, 24) = 10 carrying 60 + 5 x 5 = 85 damage, the elemental dies,
// and on_death's free starburst follows (eight meteors; its 50 MP refunded
// and spent). Detonated, the blast takes 85 off a foe beside the body.
// Perturbation (staged kit tuning nova_per_level = 0): 60 damage -> red.
TEST(KitElemental, supernova_spends_all_health_blasts_and_the_starburst_follows)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 5, 100.0f, 4);
    ASSERT_NE(nullptr, e);
    e->stats()->set_hitpoints(60.0f);
    e->set_shifter_down(1);
    ASSERT_TRUE(cast(e).ok);
    EXPECT_TRUE(e->dead());
    EXPECT_FLOAT_EQ(90.0f, mp(e)) << "10 for SUPERNOVA; the starburst's 50 refunded";
    EXPECT_EQ(8u, meteors(w).size()) << "the death starburst on top";
    const auto blasts = explosions(w);
    ASSERT_EQ(1u, blasts.size());
    walker* blast = blasts[0];
    EXPECT_EQ(e, blast->owner());
    EXPECT_EQ(10, blast->stats()->level());
    EXPECT_FLOAT_EQ(85.0f, blast->damage());
    EXPECT_EQ(ANI_EXPLODE, blast->ani_type());
    EXPECT_TRUE(blast->stats()->query_bit_flags(BIT_FIRE));
    EXPECT_EQ(centre_x(e), centre_x(blast));

    // A foe beside the body (placed after the cast, so no meteor of the
    // starburst lands on it) takes the whole blast.
    living* foe = add_living(w, FAMILY_SOLDIER, 1, 100, 120);
    ASSERT_NE(nullptr, foe);
    const float before = hp(foe);
    blast->set_dead(1);
    blast->death();
    EXPECT_TRUE(lost_one_hit_of(85.0f, before, hp(foe)));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

TEST(KitElemental, supernova_is_not_cast_with_new_specials_off)
{
    expect_off_slot_refused(4, 1);
}

// Parked on slot 4 with Shift held, a dying elemental still fires slot 1
// unshifted: the eight-meteor starburst, no SUPERNOVA blast, its slot and
// shift restored on the body, the 50 MP refunded and spent.
// Perturbation (staged living-06: on_death's set_current_special(1) line
// removed): the death casts SUPERNOVA, an explosion appears -> red.
TEST(KitElemental, dying_elemental_always_fires_slot_one)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* e = add_elemental(w, 5, 100.0f, 4);
    ASSERT_NE(nullptr, e);
    e->set_shifter_down(1);
    e->stats()->set_hitpoints(-3.0f);
    e->set_dead(1);
    e->death();
    EXPECT_EQ(8u, meteors(w).size());
    EXPECT_TRUE(explosions(w).empty()) << "no second blast";
    EXPECT_EQ(4, e->current_special());
    EXPECT_EQ(1, e->shifter_down());
    EXPECT_FLOAT_EQ(100.0f, mp(e));
    EXPECT_TRUE(e->dead());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The classic twin: with the setting off the parting shot is today's.
TEST(KitElemental, dying_elemental_with_setting_off_fires_the_classic_starburst)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(0);
    living* e = add_elemental(w, 5, 100.0f, 1);
    ASSERT_NE(nullptr, e);
    e->stats()->set_hitpoints(-3.0f);
    e->set_dead(1);
    e->death();
    EXPECT_EQ(8u, meteors(w).size());
    EXPECT_TRUE(explosions(w).empty());
    EXPECT_EQ(1, e->current_special());
    EXPECT_FLOAT_EQ(100.0f, mp(e));
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// The family file's classic hooks still answer as before with the kit
// loaded beside them: the level-up gains and the summoned elemental's toll
// on its mage (1 HP + 3 MP buy 1 HP; an owner who cannot pay both costs
// the elemental a tick of lifetime instead). Perturbations (staged
// living-06: the level-up strength gain 12 -> 13; the toll's 3 MP -> 2):
// each red.
TEST(KitElemental, classic_level_up_and_summoned_toll_are_unchanged)
{
    og::test::ScopedHookFailureGuard guard;
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_FIREELEMENTAL);
    ASSERT_NE(nullptr, fd);
    guy g(FAMILY_FIREELEMENTAL);
    const short strength = g.strength;
    og::test::level_up(*fd, &g, 1);
    EXPECT_EQ(strength + 12, g.strength);

    ElementalWorld w(1);
    living* e = add_elemental(w, 4, 0.0f, 1);
    living* mage = add_living(w, FAMILY_SOLDIER, 0, 60, 100);
    ASSERT_TRUE(e && mage);
    mage->stats()->set_magicpoints(10.0f);
    e->set_owner(mage);
    e->set_lifetime(10);
    e->stats()->set_hitpoints(50.0f);
    og::test::on_act_living(*fd, e);
    EXPECT_FLOAT_EQ(51.0f, hp(e)) << "paid in full";
    EXPECT_FLOAT_EQ(99.0f, hp(mage));
    EXPECT_FLOAT_EQ(7.0f, mp(mage));
    EXPECT_EQ(10, e->lifetime());
    mage->stats()->set_magicpoints(2.0f);
    og::test::on_act_living(*fd, e);
    EXPECT_FLOAT_EQ(51.0f, hp(e)) << "the owner cannot pay the mana";
    EXPECT_EQ(9, e->lifetime());
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// Bot gates
// ---------------------------------------------------------------------------

namespace {

bool gate(living* bot)
{
    const FamilyDescriptor* fd = get_family_descriptor(FAMILY_FIREELEMENTAL);
    return og::test::check_special_ai(*fd, bot);
}

}  // namespace

// IMMOLATE: a foe within 40, more than 60 MP, not already burning.
// Perturbation (staged kit: IMMOLATE_GATE_RANGE = 400): the far foe
// lights it -> red.
TEST(KitElemental, ai_immolate_fires_when_a_foe_is_close_and_mana_is_high)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* bot = add_elemental(w, 4, 100.0f, 2);
    ASSERT_NE(nullptr, bot);
    living* foe = add_living(w, FAMILY_SOLDIER, 1, 130, 100);
    ASSERT_NE(nullptr, foe);
    EXPECT_TRUE(gate(bot));
    EXPECT_EQ(foe, bot->foe()) << "the gate acquires the foe";
    bot->stats()->set_magicpoints(60.0f);
    EXPECT_FALSE(gate(bot)) << "60 MP is not enough";
    bot->stats()->set_magicpoints(100.0f);
    bot->set_kit_state(KIT_CHANNEL);
    EXPECT_FALSE(gate(bot)) << "already burning";
    bot->set_kit_state(0);
    place(foe, 200, 100);
    EXPECT_FALSE(gate(bot)) << "the foe is 100 px away";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// METEOR RAIN: our foe within 160 with another foe within 60 of it.
// Perturbation (staged kit: clustered >= 1): a lone foe -> red.
TEST(KitElemental, ai_meteor_fires_on_a_cluster_in_range)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* bot = add_elemental(w, 7, 200.0f, 3);
    ASSERT_NE(nullptr, bot);
    living* foe = add_living(w, FAMILY_SOLDIER, 1, 220, 100);
    ASSERT_NE(nullptr, foe);
    bot->set_foe(foe);
    EXPECT_FALSE(gate(bot)) << "a lone foe is not worth a rain";
    living* second = add_living(w, FAMILY_SOLDIER, 1, 250, 100);
    ASSERT_NE(nullptr, second);
    EXPECT_TRUE(gate(bot)) << "two foes within 60 of each other";
    place(foe, 300, 100);
    place(second, 320, 100);
    EXPECT_FALSE(gate(bot)) << "out of reach";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// REKINDLE below half; SUPERNOVA (the gate sets the shift) only below a
// fifth with two foes within 48.
// Perturbation (staged kit: NOVA_GATE_RANGE = 0): the hurt bot never
// picks SUPERNOVA -> red.
TEST(KitElemental, ai_rekindle_heals_when_hurt_and_novas_when_cornered)
{
    og::test::ScopedHookFailureGuard guard;
    ElementalWorld w(1);
    living* bot = add_elemental(w, 10, 200.0f, 4);
    ASSERT_NE(nullptr, bot);
    bot->set_shifter_down(1);
    EXPECT_FALSE(gate(bot)) << "full health";
    EXPECT_EQ(0, bot->shifter_down());
    bot->stats()->set_hitpoints(40.0f);
    EXPECT_TRUE(gate(bot));
    EXPECT_EQ(0, bot->shifter_down()) << "REKINDLE";
    bot->stats()->set_hitpoints(15.0f);
    living* one = add_living(w, FAMILY_SOLDIER, 1, 120, 100);
    ASSERT_NE(nullptr, one);
    EXPECT_TRUE(gate(bot));
    EXPECT_EQ(0, bot->shifter_down()) << "one foe: still REKINDLE";
    living* two = add_living(w, FAMILY_SOLDIER, 1, 80, 100);
    ASSERT_NE(nullptr, two);
    EXPECT_TRUE(gate(bot));
    EXPECT_EQ(1, bot->shifter_down()) << "cornered: SUPERNOVA";
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// With the setting off every new gate answers true at once: no foe
// acquired, no shift written, no draw (check_special() && !rng(3) must see
// the same answer it saw before the kit existed). STARBURST's gate is the
// classic foe-within-130 on both settings. Perturbation (staged kit: the
// three guards compare against 2 instead of 0): the gates run with the
// setting off and answer false -> red.
TEST(KitElemental, ai_off_answers_the_classic_gate)
{
    og::test::ScopedHookFailureGuard guard;
    for (int slot = 2; slot <= 4; ++slot) {
        ElementalWorld w(0);
        living* bot = add_elemental(w, 10, 10.0f, slot);
        ASSERT_NE(nullptr, bot);
        bot->stats()->set_hitpoints(10.0f);
        bot->set_shifter_down(1);
        living* foe = add_living(w, FAMILY_SOLDIER, 1, 110, 100);
        ASSERT_NE(nullptr, foe);
        const std::uint32_t state = w.world().rng_.state_;
        EXPECT_TRUE(gate(bot)) << "slot " << slot;
        EXPECT_EQ(nullptr, bot->foe()) << "slot " << slot;
        EXPECT_EQ(1, bot->shifter_down()) << "slot " << slot;
        EXPECT_EQ(state, w.world().rng_.state_) << "slot " << slot;
    }
    for (const short flag : {short{0}, short{1}}) {
        ElementalWorld w(flag);
        living* bot = add_elemental(w, 10, 100.0f, 1);
        ASSERT_NE(nullptr, bot);
        living* foe = add_living(w, FAMILY_SOLDIER, 1, 200, 100);
        ASSERT_NE(nullptr, foe);
        EXPECT_TRUE(gate(bot)) << "flag " << flag;
        place(foe, 300, 100);
        EXPECT_FALSE(gate(bot)) << "flag " << flag;
    }
    EXPECT_EQ(0u, guard.count()) << guard.message();
}

// ---------------------------------------------------------------------------
// The ember family: art and glyph
// ---------------------------------------------------------------------------

// Perturbation (staged effect-16: glyph_ascii = "."): red.
TEST(KitElemental, new_kit_entities_load_art_and_glyphs)
{
    const int ember_family = fx_family("core:ember");
    ASSERT_EQ(16, ember_family) << "core:ember is effect wire id 16";
    const EffectFamilyDescriptor* efd =
        get_effect_family_descriptor(ember_family);
    ASSERT_NE(nullptr, efd);
    ASSERT_NE(nullptr, efd->pix_filename);
    EXPECT_STREQ("ember.png", efd->pix_filename);
    EXPECT_EQ(U'∴', efd->glyph.codepoint);
    EXPECT_EQ(':', efd->glyph.ascii);
    EXPECT_EQ(og::GlyphColor::Red, efd->glyph.color);
    EXPECT_TRUE(efd->glyph.bold);
    EXPECT_FALSE(efd->glyph.transparent) << "an ember is seen on every client";

    ElementalWorld w(1);
    walker* ember = w.world().add_ob(Order::FX, ember_family);
    ASSERT_NE(nullptr, ember);
    EXPECT_EQ(7, ember->sizex());
    EXPECT_EQ(7, ember->sizey());
    EXPECT_EQ(1, ember->set_frame(1)) << "two frames: live and dying";
}

// ---------------------------------------------------------------------------
// A bot elemental in the real tick loop
// ---------------------------------------------------------------------------

// Two runs of the same level-10 bot elemental with mana to spare beside
// three foes, same RNG seed, 300 world ticks of the real act loop: with the
// setting on it reaches for its new specials (an IMMOLATE fire, a METEOR
// RAIN or a REKINDLE leaves a marker, an ember, an explosion or a flash
// behind); with the setting off none of them ever appears.
// Perturbation (staged lib/kit_marker.lua: spawn makes no marker): no fire,
// no rain, no ember -> red.
TEST(KitElemental, bot_elemental_uses_its_kit_in_the_tick_loop_only_when_on)
{
    og::test::ScopedHookFailureGuard guard;
    struct Seen {
        int markers = 0;
        int embers = 0;
        int explosions = 0;
        int flashes = 0;
    };
    const auto run = [](short flag) {
        Seen seen;
        ElementalWorld w(flag);
        living* bot = add_elemental(w, 10, 1000.0f, 1);
        if (bot == nullptr)
            return seen;
        bot->stats()->set_max_hitpoints(1000.0f);
        bot->stats()->set_hitpoints(450.0f);
        bot->set_act_type(ACT_RANDOM);
        for (int i = 0; i < 3; ++i) {
            living* foe = add_living(w, FAMILY_SOLDIER, 1,
                                     220 + 16 * i, 100, 1);
            if (foe == nullptr)
                continue;
            foe->set_act_type(ACT_RANDOM);
            foe->stats()->set_max_hitpoints(5000.0f);
            foe->stats()->set_hitpoints(5000.0f);
        }
        w.world().rng_.state_ = 12345u;
        for (int tick = 0; tick < 300 && !bot->dead(); ++tick) {
            w.world().tick();
            seen.markers = std::max<int>(seen.markers, static_cast<int>(markers(w).size()));
            seen.embers = std::max<int>(seen.embers, static_cast<int>(embers(w).size()));
            seen.explosions += static_cast<int>(explosions(w).size());
            seen.flashes += static_cast<int>(live_of(w, Order::FX, FAMILY_HIT).size());
        }
        return seen;
    };
    const Seen on = run(1);
    const Seen off = run(0);
    if (std::getenv("OG_KIT_PLAYTEST") != nullptr)
        std::printf("bot elemental, 300 ticks: on markers=%d embers=%d "
                    "explosion-ticks=%d flash-ticks=%d | off markers=%d "
                    "embers=%d explosion-ticks=%d\n",
                    on.markers, on.embers, on.explosions, on.flashes,
                    off.markers, off.embers, off.explosions);
    EXPECT_GT(on.markers + on.embers, 0)
        << "with the setting on the bot casts a kit special";
    EXPECT_EQ(0, off.markers);
    EXPECT_EQ(0, off.embers);
    EXPECT_EQ(0u, guard.count()) << guard.message();
}
