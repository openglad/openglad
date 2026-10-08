/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// New Specials: hidden walkers (KIT_HIDDEN) — inert, skipped by every
// finder and claim scan, untouchable, out of the collision table — the
// hidden seat rule, and the REASSEMBLE ward consumed at the top of
// walker::death. Every case flips back when the bit is cleared, which is
// what proves each answer comes from the hidden bit and nothing else.

#include <gtest/gtest.h>

#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/obmap.h>
#include <openglad/gameplay/respawn/respawn_state.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/sim_input_handler.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>

#include <algorithm>
#include <cstdint>
#include <list>
#include <memory>
#include <string>

namespace {

walker* add_living(TestGameWorld& tw, int family, unsigned char team,
                   short x, short y)
{
    walker* w = tw.world().add_ob(Order::Living, family);
    if (w == nullptr)
        return nullptr;
    w->set_team_num(team);
    w->setxy(x, y);
    w->set_act_type(ACT_GUARD);  // hold still: positions stay pinned
    w->stats()->set_max_hitpoints(40.0f);
    w->stats()->set_hitpoints(40.0f);
    return w;
}

void open_field(TestGameWorld& tw)
{
    GameWorld& world = tw.world();
    world.resize_grid(32, 32);
    std::fill_n(world.grid.data.get(),
                static_cast<std::size_t>(world.grid.w) * world.grid.h,
                static_cast<std::uint8_t>(PIX_GRASS1));
    world.my_team = 0;
}

bool in_obmap(TestGameWorld& tw, walker* w)
{
    return tw.world().myobmap->walker_to_pos.count(w) != 0;
}

bool has(const std::list<walker*>& pile, const walker* w)
{
    return std::find(pile.begin(), pile.end(), w) != pile.end();
}

int count_alive(const TestGameWorld& tw, Order order, int family)
{
    int n = 0;
    for (const auto* list : {&tw.world().oblist, &tw.world().fxlist})
        for (const auto& uptr : *list)
            if (uptr && !uptr->dead() && uptr->query_order() == order &&
                uptr->family() == family)
                ++n;
    return n;
}

}  // namespace

// A hidden orc beside a soldier: no finder returns it, it is out of the
// obmap, it does not act (its per-tick drawcycle stands still), a foe that
// had locked onto it lets go, and setxy / change_floor / the obmap
// re-registration helper leave it out. Revealed, every answer flips.
TEST(KitSeams, hidden_walker_is_inert_and_skipped_by_every_finder)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* soldier = add_living(tw, FAMILY_SOLDIER, 0, 64, 64);
    walker* orc = add_living(tw, FAMILY_ORC, 1, 84, 64);
    walker* far_orc = add_living(tw, FAMILY_ORC, 1, 160, 64);
    ASSERT_TRUE(soldier && orc && far_orc);
    orc->set_user(1);  // a seat-driven orc, so the player finder sees it

    const auto check = [&](bool hidden_now) {
        const bool seen = !hidden_now;
        std::int32_t n = 0;
        EXPECT_EQ(seen ? orc : far_orc, world.find_nearest_foe(soldier))
            << "find_nearest_foe, hidden=" << hidden_now;
        EXPECT_EQ(seen, has(world.find_foes_in_range(world.oblist, 400, &n, soldier), orc))
            << "find_foes_in_range, hidden=" << hidden_now;
        EXPECT_EQ(seen, has(world.find_in_range(world.oblist, 400, &n, soldier), orc))
            << "find_in_range, hidden=" << hidden_now;
        EXPECT_EQ(seen, has(world.find_friends_in_range(world.oblist, 400, &n, far_orc), orc))
            << "find_friends_in_range, hidden=" << hidden_now;
        EXPECT_EQ(seen, world.find_nearest_player(soldier) == orc)
            << "find_nearest_player, hidden=" << hidden_now;
        EXPECT_EQ(seen, in_obmap(tw, orc)) << "obmap, hidden=" << hidden_now;
    };

    check(false);
    orc->set_hidden(true);
    check(true);

    // Moving a hidden walker, or changing its floor, keeps it out.
    orc->setxy(88, 64);
    EXPECT_FALSE(in_obmap(tw, orc)) << "setxy leaves a hidden walker out";
    orc->setworldxy(86.0f, 64.0f);
    EXPECT_FALSE(in_obmap(tw, orc)) << "setworldxy leaves it out";
    orc->change_floor(1);
    orc->change_floor(0);
    EXPECT_FALSE(in_obmap(tw, orc)) << "change_floor leaves it out";
    og::sim::ensure_obmap_registration(world, orc);
    EXPECT_FALSE(in_obmap(tw, orc))
        << "the re-registration helper never registers a hidden walker";

    // Inert: a world tick acts every other walker, not the hidden one; the
    // soldier that had locked onto it lets go.
    soldier->set_foe(orc);
    const unsigned char orc_cycle = orc->drawcycle();
    const unsigned char far_cycle = far_orc->drawcycle();
    world.tick();
    EXPECT_EQ(orc_cycle, orc->drawcycle()) << "a hidden walker does not act";
    EXPECT_NE(far_cycle, far_orc->drawcycle()) << "a visible one does";
    EXPECT_NE(orc, soldier->foe()) << "a foe lock on a hidden walker drops";
    EXPECT_EQ(0, world.level_done) << "a hidden hostile holds the level open";

    orc->set_hidden(false);
    check(false);
    world.tick();
    EXPECT_NE(orc_cycle, orc->drawcycle()) << "revealed, it acts again";
}

// The two public landing probes the respawn engine and the floor change use
// treat a hidden walker as not there (it is not in the obmap piles either).
TEST(KitSeams, landing_probes_ignore_a_hidden_walker)
{
    TestGameWorld tw;
    open_field(tw);
    GameWorld& world = tw.world();
    walker* lander = add_living(tw, FAMILY_SOLDIER, 0, 200, 200);
    walker* sitter = add_living(tw, FAMILY_ORC, 1, 96, 96);
    ASSERT_TRUE(lander && sitter);

    EXPECT_FALSE(og::sim::respawn_spot_clear(world, lander, 96, 96, 0));
    EXPECT_FALSE(world.floor_landing_clear(lander, 96.0f, 96.0f, 0));
    sitter->set_hidden(true);
    EXPECT_TRUE(og::sim::respawn_spot_clear(world, lander, 96, 96, 0))
        << "a hidden walker does not block a respawn spot";
    EXPECT_TRUE(world.floor_landing_clear(lander, 96.0f, 96.0f, 0))
        << "nor a floor landing";

    // The probes do not lean on the obmap alone: a hidden walker that is
    // somehow still filed in it (the invariant broken) blocks nothing.
    world.myobmap->add(sitter, sitter->xpos(), sitter->ypos());
    ASSERT_TRUE(in_obmap(tw, sitter));
    EXPECT_TRUE(og::sim::respawn_spot_clear(world, lander, 96, 96, 0))
        << "the respawn probe skips a hidden walker itself";
    EXPECT_TRUE(world.floor_landing_clear(lander, 96.0f, 96.0f, 0))
        << "and so does the floor-landing probe";
    world.myobmap->remove(sitter);
}

namespace {

// Seats a walker for player 0 and drives sim_process_player_input.
struct Seat {
    walker* control;
    SimInputDebounce debounce{};
    InputState input;
    TestGameWorld& tw;

    Seat(TestGameWorld& world, walker* w) : control(w), tw(world)
    {
        input.clear();
        w->set_user(0);
        w->set_act_type(ACT_CONTROL);
    }
    void press(InputAction a)
    {
        input.players[0].pressed[static_cast<int>(a)] = true;
        input.players[0].held[static_cast<int>(a)] = true;
    }
    SimInputResult run()
    {
        SimInputResult r = sim_process_player_input(
            input.players[0], control, tw.world(), 0, 0, debounce,
            &tw.events);
        input.clear();
        return r;
    }
};

}  // namespace

// A hidden hero answers Special (here the orc's HOWL, free while hidden)
// and Switch Character; walking, firing and yelling do nothing. Visible,
// the same input walks, fires and yells.
TEST(KitSeams, hidden_control_reads_only_special_and_switch)
{
    TestGameWorld tw;
    open_field(tw);
    walker* orc = add_living(tw, FAMILY_ORC, 0, 96, 96);
    walker* ally = add_living(tw, FAMILY_SOLDIER, 0, 200, 200);
    ASSERT_TRUE(orc && ally);
    // Facing up and heading up, so a Fire starts the attack row at once.
    for (walker* w : {orc, ally}) {
        w->set_stepsize(2.0f);
        w->set_lastx(0.0f);
        w->set_lasty(-2.0f);
        w->set_curdir(FACE_UP);
        w->set_enddir(FACE_UP);
    }
    orc->stats()->set_max_magicpoints(100.0f);
    orc->stats()->set_magicpoints(0.0f);
    orc->set_current_special(1);
    Seat seat(tw, orc);

    orc->set_hidden(true);
    seat.press(InputAction::MoveRight);
    seat.press(InputAction::Fire);
    seat.press(InputAction::Yell);
    seat.run();
    EXPECT_EQ(96, orc->xpos()) << "a hidden hero does not walk";
    EXPECT_FLOAT_EQ(0.0f, orc->lastx()) << "not even a step's heading";
    EXPECT_EQ(ANI_WALK, orc->ani_type()) << "nor fire";
    EXPECT_EQ(0, orc->yo_delay()) << "nor yell";

    seat.press(InputAction::Special);
    seat.run();
    EXPECT_FLOAT_EQ(2.0f, orc->busy())
        << "Special is dispatched (HOWL's busy), at 0 MP: a hidden press is free";
    EXPECT_FLOAT_EQ(0.0f, orc->stats()->magicpoints());

    seat.press(InputAction::SwitchChar);
    seat.run();
    EXPECT_EQ(ally, seat.control) << "Switch Character leaves a hidden hero";

    // The same input on a visible hero walks, fires and yells.
    walker* visible = add_living(tw, FAMILY_ORC, 0, 96, 160);
    ASSERT_NE(nullptr, visible);
    visible->set_stepsize(2.0f);
    visible->set_lastx(0.0f);
    visible->set_lasty(-2.0f);
    visible->set_curdir(FACE_UP);
    visible->set_enddir(FACE_UP);
    ally->set_user(-1);
    Seat open(tw, visible);
    open.press(InputAction::Fire);
    open.press(InputAction::Yell);
    open.run();
    EXPECT_EQ(ANI_ATTACK, visible->ani_type()) << "a visible hero fires";
    EXPECT_EQ(30, visible->yo_delay()) << "and yells";
    open.press(InputAction::MoveRight);
    open.run();
    EXPECT_FLOAT_EQ(2.0f, visible->lastx()) << "and steps";
}

namespace {

// Registers `body` as core:soldier's do_special for the test's lifetime.
class ScopedSoldierSpecial {
public:
    explicit ScopedSoldierSpecial(const std::string& body)
    {
        init_all_registries();
        og::script::clear_pack_scripts();
        og::script::register_pack_script(
            {"test.kit", "kit_hide_probe.lua",
             "og.register_hooks('living', 'core:soldier', {\n"
             "  do_special = function(self)\n" +
                 body +
                 "\n    return true\n"
                 "  end,\n"
                 "})\n"});
    }
    ~ScopedSoldierSpecial() { og::script::clear_pack_scripts(); }
    ScopedSoldierSpecial(const ScopedSoldierSpecial&) = delete;
    ScopedSoldierSpecial& operator=(const ScopedSoldierSpecial&) = delete;

private:
    og::test::ScopedPackStoreState pack_store_;
};

}  // namespace

// A cast that hides its caster (a ghost entering a body is one) ends the
// seat's input on that tick: the Fire and the walk pressed with it do
// nothing. A cast that does not hide leaves them alone.
TEST(KitSeams, a_cast_that_hides_its_caster_ends_the_tick)
{
    ScopedSoldierSpecial special("    self:set_hidden(1)");
    TestGameWorld tw;
    open_field(tw);
    walker* soldier = add_living(tw, FAMILY_SOLDIER, 0, 96, 96);
    ASSERT_NE(nullptr, soldier);
    soldier->stats()->set_max_magicpoints(500.0f);
    soldier->stats()->set_magicpoints(500.0f);
    soldier->set_current_special(1);
    // Facing up and heading up, so a Fire would start the attack row at once.
    soldier->set_stepsize(2.0f);
    soldier->set_lastx(0.0f);
    soldier->set_lasty(-2.0f);
    soldier->set_curdir(FACE_UP);
    soldier->set_enddir(FACE_UP);
    Seat seat(tw, soldier);
    seat.press(InputAction::Special);
    seat.press(InputAction::Fire);
    seat.press(InputAction::MoveRight);
    seat.run();
    ASSERT_TRUE(soldier->hidden()) << "the cast ran";
    EXPECT_FLOAT_EQ(0.0f, soldier->lastx())
        << "no step after the cast hid the caster";
    EXPECT_EQ(ANI_WALK, soldier->ani_type()) << "no fire either";

    // The held arm (a key still down from an earlier tick) ends the tick
    // the same way when its cast hides the caster: once with a held walk,
    // once with a held Fire, each from the same facing as above.
    const auto held_cast = [&](InputAction with) {
        soldier->set_hidden(false);
        ASSERT_FALSE(soldier->hidden());
        soldier->set_lastx(0.0f);
        soldier->set_lasty(-2.0f);
        soldier->set_curdir(FACE_UP);
        soldier->set_enddir(FACE_UP);
        seat.input.players[0].held[static_cast<int>(InputAction::Special)] = true;
        seat.input.players[0].held[static_cast<int>(with)] = true;
        seat.run();
        ASSERT_TRUE(soldier->hidden()) << "the held cast ran";
    };
    held_cast(InputAction::MoveRight);
    EXPECT_FLOAT_EQ(0.0f, soldier->lastx())
        << "no step after the held cast hid the caster";
    held_cast(InputAction::Fire);
    EXPECT_EQ(ANI_WALK, soldier->ani_type()) << "no held fire either";
}

// REASSEMBLE: a warded walker's death is cancelled at the top of
// walker::death, before death_called, the obmap removal, the life gem and
// the blood stain: it stands at a quarter health with the bit spent. The
// next death is final. The skeleton leaves no stain at all, so the stain is
// proved on a warded soldier, which leaves one when it dies for good.
TEST(KitSeams, ward_revives_before_the_gem_and_the_obmap_removal)
{
    TestGameWorld tw;
    open_field(tw);
    walker* skeleton = add_living(tw, FAMILY_SKELETON, 0, 96, 96);
    ASSERT_NE(nullptr, skeleton);
    walker* soldier = add_living(tw, FAMILY_SOLDIER, 0, 160, 96);
    ASSERT_NE(nullptr, soldier);
    soldier->set_kit_state(KIT_WARD);
    soldier->stats()->set_hitpoints(-5.0f);
    soldier->set_dead(1);
    soldier->death();
    EXPECT_FALSE(soldier->dead()) << "the warded soldier stands back up";
    EXPECT_EQ(0, count_alive(tw, Order::Treasure, FAMILY_STAIN))
        << "and leaves no stain";
    soldier->stats()->set_hitpoints(-5.0f);
    soldier->set_dead(1);
    soldier->death();
    EXPECT_EQ(1, count_alive(tw, Order::Treasure, FAMILY_STAIN))
        << "its final death leaves one";
    skeleton->set_owned_myguy(std::make_unique<guy>(FAMILY_SKELETON));
    skeleton->set_kit_state(KIT_WARD | KIT_FEARLESS);
    skeleton->stats()->set_hitpoints(-5.0f);
    skeleton->set_dead(1);

    EXPECT_FALSE(skeleton->death());
    EXPECT_FALSE(skeleton->dead()) << "it stands back up";
    EXPECT_EQ(0, skeleton->death_called());
    EXPECT_FLOAT_EQ(10.0f, skeleton->stats()->hitpoints()) << "max/4";
    EXPECT_EQ(KIT_FEARLESS, skeleton->kit_state()) << "only the ward is spent";
    EXPECT_TRUE(in_obmap(tw, skeleton)) << "never left the obmap";
    EXPECT_EQ(ANI_TELE_IN, skeleton->ani_type()) << "the grow row plays";
    EXPECT_EQ(0, count_alive(tw, Order::Treasure, FAMILY_LIFE_GEM))
        << "no life gem";
    const auto& events = tw.events.events();
    EXPECT_TRUE(std::any_of(events.begin(), events.end(), [](const auto& e) {
        return e.kind == og::sim::EventKind::Notification &&
               e.text.ends_with(" reassembles!");
    }));

    skeleton->stats()->set_hitpoints(-5.0f);
    skeleton->set_dead(1);
    skeleton->death();
    EXPECT_TRUE(skeleton->dead()) << "unwarded, the death is final";
    EXPECT_EQ(1, skeleton->death_called());
    EXPECT_FALSE(in_obmap(tw, skeleton));
    EXPECT_EQ(1, count_alive(tw, Order::Treasure, FAMILY_LIFE_GEM))
        << "and the hero's gem drops";
}
