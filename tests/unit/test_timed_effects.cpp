/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
// The seat's countdown (og::sim::seat_timer, timed_effects.h): which of a
// walker's New Specials effects the HUDs show, and that a mirror built
// from a snapshot answers the same. Also the one list of kit marker roles
// (kit_marker_role.h) as the Lua kits read it, og.C.MARKER_*.
//
// The real casts (PHASE, DIG IN, LEGION, IMMOLATE) go through
// walker::special() on a world with the setting on, so a marker made by the
// shipped kits is what the model reads; a ward marker and a few odd ones
// are made by hand.
#include <gtest/gtest.h>

#include "../test_family_hook_dispatch.h"
#include "../test_game_world_fixture.h"
#include "unit_pack_store_guard.h"

#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/families/family_registries.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/families/family_string_ids.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/kit_marker_role.h>
#include <openglad/gameplay/kit_state.h>
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/script/family_hooks.h>
#include <openglad/gameplay/script/pack_scripts.h>
#include <openglad/gameplay/script/script_host.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/timed_effects.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/world_snapshot.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace {

using og::sim::seat_timer;
using og::sim::SeatTimer;

int kit_marker_family()
{
    return og::families::resolve_family_string_id(Order::FX,
                                                  "core:kit_marker");
}

std::string label_of(const SeatTimer& t)
{
    return t.label != nullptr ? std::string(t.label) : std::string("(none)");
}

class TimedEffects : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    std::unique_ptr<TestGameWorld> tw_;
    og::test::ScopedHookFailureGuard guard_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        // A test elsewhere in the binary may have reset the mod slots that
        // hold the kits' families: refresh before the world's loader is
        // built (the KitSkeleton fixture's rule).
        if (kit_marker_family() != 17)
            (void)og::resources::refresh_pack_scripts();
        ASSERT_EQ(17, kit_marker_family());
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(32, 32);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.new_specials = 1;
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    GameWorld& world() { return tw_->world(); }

    // A living that takes no decisions of its own (no seat, no AI), with
    // plenty of mana and no casting animation to finish.
    walker* add_living(int family, unsigned char team, short x, short y,
                       int level, float mp = 300.0f)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->setxy(x, y);
        w->set_act_type(ACT_CONTROL);
        w->set_ani_type(ANI_WALK);
        w->set_cycle(0);
        w->stats()->set_level(level);
        w->stats()->set_max_hitpoints(100.0f);
        w->stats()->set_hitpoints(100.0f);
        w->stats()->set_max_magicpoints(mp);
        w->stats()->set_magicpoints(mp);
        return w;
    }

    // A kit marker made by hand: the fields a marker carries and nothing
    // else (it is never acted in these tests).
    walker* add_marker(walker* owner, std::uint8_t role, int lifetime)
    {
        walker* m = world().add_ob(Order::FX, kit_marker_family());
        if (m == nullptr)
            return nullptr;
        m->set_owner(owner);
        m->set_team_num(owner != nullptr ? owner->team_num() : 0);
        m->set_ani_type(static_cast<char>(role));
        m->set_lifetime(lifetime);
        m->set_ignore(1);
        return m;
    }

    // The owner's live kit marker (the one a cast just made).
    walker* marker_of(const walker* owner)
    {
        for (const auto& up : world().oblist)
            if (up && !up->dead() && up->query_order() == Order::FX &&
                static_cast<int>(static_cast<unsigned char>(up->family())) ==
                    kit_marker_family() &&
                up->owner() == owner)
                return up.get();
        return nullptr;
    }

    std::string cast(walker* w, int slot, short shift)
    {
        w->set_current_special(static_cast<char>(slot));
        w->set_shifter_down(shift);
        std::string reason;
        const bool ok = w->special(nullptr, &reason);
        w->set_shifter_down(0);
        return ok ? std::string() : (reason.empty() ? "refused" : reason);
    }
};

}  // namespace

// Every effect the HUD shows, made the way a player makes it, answers its
// own label and the clock it runs on.
//
// RED (run by hand): the "DIG IN" label spelled "DIG" in timed_effects.cpp
// -> the DIG IN row reads DIG. Drop MARKER_PHASE_VEIL from kKitConstants in
// bindings_kit.cpp -> the core pack's kit_marker.lua reads nil for the
// phase role, the veil never gets one, and the PHASE row reads (none).
TEST_F(TimedEffects, each_running_effect_answers_its_label_and_clock)
{
    struct Row {
        const char* label;
        walker* who;
    };
    std::vector<Row> rows;

    // PHASE: a level-10 ghost's slot 4.
    walker* ghost = add_living(FAMILY_GHOST, 0, 64, 64, 10);
    ASSERT_NE(nullptr, ghost);
    ASSERT_EQ("", cast(ghost, 4, 0));
    rows.push_back({"PHASE", ghost});

    // DIG IN: a level-4 skeleton's slot 2.
    walker* digger = add_living(FAMILY_SKELETON, 0, 128, 64, 4);
    ASSERT_NE(nullptr, digger);
    ASSERT_EQ("", cast(digger, 2, 0));
    rows.push_back({"DIG IN", digger});

    // LEGION: a level-10 skeleton's Shift + slot 4.
    walker* legion = add_living(FAMILY_SKELETON, 0, 192, 64, 10);
    ASSERT_NE(nullptr, legion);
    ASSERT_EQ("", cast(legion, 4, 1));
    rows.push_back({"LEGION", legion});

    // IMMOLATE: a level-4 fire elemental's slot 2.
    walker* fire = add_living(FAMILY_FIREELEMENTAL, 0, 256, 64, 4);
    ASSERT_NE(nullptr, fire);
    ASSERT_EQ("", cast(fire, 2, 0));
    rows.push_back({"IMMOLATE", fire});

    for (const Row& row : rows) {
        const walker* marker = marker_of(row.who);
        ASSERT_NE(nullptr, marker) << row.label << ": the cast made a marker";
        ASSERT_GT(marker->lifetime(), 0) << row.label;
        const SeatTimer t = seat_timer(world(), *row.who);
        EXPECT_EQ(std::string(row.label), label_of(t));
        EXPECT_EQ(marker->lifetime(), t.ticks)
            << row.label << ": the marker's own clock";
    }

    // REASSEMBLE: the ward's marker (role 6).
    walker* warded = add_living(FAMILY_SKELETON, 0, 64, 128, 10);
    ASSERT_NE(nullptr, add_marker(warded, MARKER_WARD, 123));
    SeatTimer t = seat_timer(world(), *warded);
    EXPECT_EQ("REASSEMBLE", label_of(t));
    EXPECT_EQ(123, t.ticks);

    // POSSESS: the host carries the clock.
    walker* rider = add_living(FAMILY_GHOST, 0, 128, 128, 10);
    walker* host = add_living(FAMILY_ORC, 1, 160, 128, 1);
    ASSERT_TRUE(rider != nullptr && host != nullptr);
    host->set_act_type(ACT_GUARD);
    ASSERT_TRUE(og::sim::possess(world(), *rider, *host, 580).ok);
    t = seat_timer(world(), *host);
    EXPECT_EQ("POSSESS", label_of(t));
    EXPECT_EQ(580, t.ticks);
    EXPECT_EQ(nullptr, seat_timer(world(), *rider).label)
        << "the hidden rider has no clock of its own: its seat is the host";

    // HASTE: the speed bonus (a potion or a faerie's HASTEN).
    walker* quick = add_living(FAMILY_SOLDIER, 0, 64, 192, 3);
    quick->set_speed_bonus_left(75);
    t = seat_timer(world(), *quick);
    EXPECT_EQ("HASTE", label_of(t));
    EXPECT_EQ(75, t.ticks);
}

// Several at once: the one ending soonest is shown, whichever it is.
//
// RED (run by hand): `ticks < timer.ticks` flipped to `>` in Best::offer ->
// the LEGION 300 row wins over the ward's 100.
TEST_F(TimedEffects, the_soonest_ending_effect_is_shown)
{
    walker* skel = add_living(FAMILY_SKELETON, 0, 64, 64, 10);
    ASSERT_NE(nullptr, skel);
    walker* legion = add_marker(skel, MARKER_LEGION, 300);
    walker* ward = add_marker(skel, MARKER_WARD, 100);
    ASSERT_TRUE(legion != nullptr && ward != nullptr);

    SeatTimer t = seat_timer(world(), *skel);
    EXPECT_EQ("REASSEMBLE", label_of(t));
    EXPECT_EQ(100, t.ticks);

    ward->set_lifetime(400);
    t = seat_timer(world(), *skel);
    EXPECT_EQ("LEGION", label_of(t));
    EXPECT_EQ(300, t.ticks);

    skel->set_speed_bonus_left(299);
    t = seat_timer(world(), *skel);
    EXPECT_EQ("HASTE", label_of(t)) << "a haste one tick sooner";
    EXPECT_EQ(299, t.ticks);
}

// A tie goes to the table order: POSSESS, PHASE, DIG IN, REASSEMBLE,
// IMMOLATE, LEGION, HASTE.
//
// RED (run by hand): `candidate_rank < rank` flipped to `>` -> every row
// below answers the last of its tied effects instead of the first.
TEST_F(TimedEffects, a_tie_goes_to_the_table_order)
{
    walker* rider = add_living(FAMILY_GHOST, 0, 64, 64, 10);
    walker* host = add_living(FAMILY_ORC, 1, 96, 64, 1);
    ASSERT_TRUE(rider != nullptr && host != nullptr);
    host->set_act_type(ACT_GUARD);
    ASSERT_TRUE(og::sim::possess(world(), *rider, *host, 50).ok);
    host->set_speed_bonus_left(50);
    // The walker's own markers, listed in the reverse of the table order so
    // the list order cannot decide the tie.
    walker* legion = add_marker(host, MARKER_LEGION, 50);
    walker* fire = add_marker(host, MARKER_IMMOLATION, 50);
    walker* ward = add_marker(host, MARKER_WARD, 50);
    walker* burrow = add_marker(host, MARKER_BURROW, 50);
    walker* veil = add_marker(host, MARKER_PHASE_VEIL, 50);
    ASSERT_TRUE(legion && fire && ward && burrow && veil);

    const char* expected[] = {"POSSESS", "PHASE", "DIG IN", "REASSEMBLE",
                              "IMMOLATE", "LEGION", "HASTE"};
    // Retire the winner each round and the next in the order steps up.
    std::vector<std::function<void()>> retire = {
        [&] { host->set_possess_ticks(0); },
        [&] { veil->set_dead(1); },
        [&] { burrow->set_dead(1); },
        [&] { ward->set_dead(1); },
        [&] { fire->set_dead(1); },
        [&] { legion->set_dead(1); },
        [&] { host->set_speed_bonus_left(0); },
    };
    for (std::size_t i = 0; i < retire.size(); ++i) {
        const SeatTimer t = seat_timer(world(), *host);
        EXPECT_EQ(std::string(expected[i]), label_of(t)) << "round " << i;
        EXPECT_EQ(50, t.ticks) << "round " << i;
        retire[i]();
    }
    EXPECT_EQ(nullptr, seat_timer(world(), *host).label)
        << "with everything retired nothing is shown";
}

// With the setting off nothing is ever shown, a speed potion included, so
// the classic HUD does not change.
//
// RED (run by hand): the `world.new_specials == 0` return deleted -> the
// host reads POSSESS and the potion drinker HASTE with the setting off.
TEST_F(TimedEffects, nothing_is_shown_with_the_setting_off)
{
    walker* rider = add_living(FAMILY_GHOST, 0, 64, 64, 10);
    walker* host = add_living(FAMILY_ORC, 1, 96, 64, 1);
    ASSERT_TRUE(rider != nullptr && host != nullptr);
    host->set_act_type(ACT_GUARD);
    ASSERT_TRUE(og::sim::possess(world(), *rider, *host, 200).ok);
    ASSERT_NE(nullptr, add_marker(host, MARKER_LEGION, 300));
    walker* potion = add_living(FAMILY_SOLDIER, 0, 64, 128, 3);
    potion->set_speed_bonus_left(60);
    ASSERT_EQ("POSSESS", label_of(seat_timer(world(), *host)));

    world().new_specials = 0;
    EXPECT_EQ(nullptr, seat_timer(world(), *host).label);
    EXPECT_EQ(0, seat_timer(world(), *host).ticks);
    EXPECT_EQ(nullptr, seat_timer(world(), *potion).label)
        << "the classic potion keeps the classic HUD";
}

// The model reads the walker's OWN markers, and only the running, counting
// marker of a role it shows.
//
// RED (run by hand): the `ob->owner() != &control` test dropped -> the
// bystander reads the skeleton's LEGION.
TEST_F(TimedEffects, only_the_walkers_own_running_markers_count)
{
    walker* skel = add_living(FAMILY_SKELETON, 0, 64, 64, 10);
    walker* bystander = add_living(FAMILY_SOLDIER, 0, 96, 64, 3);
    ASSERT_TRUE(skel != nullptr && bystander != nullptr);
    ASSERT_NE(nullptr, add_marker(skel, MARKER_LEGION, 240));
    EXPECT_EQ("LEGION", label_of(seat_timer(world(), *skel)));
    EXPECT_EQ(nullptr, seat_timer(world(), *bystander).label)
        << "another walker's marker is not mine";

    // Things a marker can be that are not a countdown.
    walker* rain = add_marker(bystander, MARKER_METEOR_RAIN, 60);
    walker* spent = add_marker(bystander, MARKER_BURROW, 0);
    walker* gone = add_marker(bystander, MARKER_WARD, 90);
    ASSERT_TRUE(rain && spent && gone);
    gone->set_dead(1);
    // An effect of another family the bystander owns (a scare burst), with a
    // live lifetime: not a kit marker.
    walker* scare = world().add_ob(
        Order::FX, og::families::resolve_family_string_id(
                       Order::FX, "core:ghost_scare"));
    ASSERT_NE(nullptr, scare);
    scare->set_owner(bystander);
    scare->set_ani_type(static_cast<char>(MARKER_WARD));
    scare->set_lifetime(30);
    EXPECT_EQ(nullptr, seat_timer(world(), *bystander).label)
        << "a meteor rain, a spent clock, a dead marker and a scare are "
           "not countdowns";

    // A possession with no clock (the engine's "for good", which no core kit
    // asks for) shows nothing either.
    walker* rider = add_living(FAMILY_GHOST, 0, 128, 64, 10);
    walker* host = add_living(FAMILY_ORC, 1, 160, 64, 1);
    ASSERT_TRUE(rider != nullptr && host != nullptr);
    host->set_act_type(ACT_GUARD);
    ASSERT_TRUE(og::sim::possess(world(), *rider, *host, 0).ok);
    EXPECT_EQ(nullptr, seat_timer(world(), *host).label);
}

// The model does not look at who drives the walker: a bot's effect is
// answered too (the SDL HUD's own gate decides whom to show it to).
TEST_F(TimedEffects, a_bots_effect_is_answered_too)
{
    walker* bot = add_living(FAMILY_SKELETON, 1, 64, 64, 10);
    ASSERT_NE(nullptr, bot);
    bot->set_user(-1);
    bot->set_act_type(ACT_GUARD);
    ASSERT_NE(nullptr, add_marker(bot, MARKER_WARD, 77));
    const SeatTimer t = seat_timer(world(), *bot);
    EXPECT_EQ("REASSEMBLE", label_of(t));
    EXPECT_EQ(77, t.ticks);
}

// Seconds round up at 12 ticks a second, the SCARED line's rule, so a clock
// never reads 0s while something still runs.
TEST(TimedEffectsSeconds, ticks_round_up_to_whole_seconds)
{
    EXPECT_EQ(0, og::sim::timer_seconds(0));
    EXPECT_EQ(1, og::sim::timer_seconds(1));
    EXPECT_EQ(1, og::sim::timer_seconds(12));
    EXPECT_EQ(2, og::sim::timer_seconds(13));
    EXPECT_EQ(30, og::sim::timer_seconds(360));
    EXPECT_EQ(49, og::sim::timer_seconds(580));
    EXPECT_EQ(60, og::sim::timer_seconds(720));
}

// The countdown is read off snapshot fields only: a fresh world built from
// a serialized keyframe answers what the server's world answers.
//
// RED (run by hand): the owner compare in seat_timer dropped -> the
// mirror's possessed orc reads the skeleton's REASSEMBLE 123 (sooner than
// its own 456), and so does the server's.
TEST_F(TimedEffects, a_mirror_built_from_a_snapshot_answers_the_same)
{
    walker* skel = add_living(FAMILY_SKELETON, 0, 64, 64, 10);
    walker* rider = add_living(FAMILY_GHOST, 0, 128, 64, 10);
    walker* host = add_living(FAMILY_ORC, 1, 160, 64, 1);
    ASSERT_TRUE(skel && rider && host);
    host->set_act_type(ACT_GUARD);
    ASSERT_NE(nullptr, add_marker(skel, MARKER_WARD, 123));
    ASSERT_TRUE(og::sim::possess(world(), *rider, *host, 456).ok);
    const std::uint32_t skel_id = skel->entity_id();
    const std::uint32_t host_id = host->entity_id();
    const std::uint32_t rider_id = rider->entity_id();
    ASSERT_EQ("REASSEMBLE", label_of(seat_timer(world(), *skel)));
    ASSERT_EQ("POSSESS", label_of(seat_timer(world(), *host)));

    const og::sim::WorldSnapshot decoded = og::sim::deserialize_snapshot(
        og::sim::serialize_snapshot(og::sim::capture_keyframe_snapshot(world())));

    TestGameWorld mirror_fx;
    GameWorld& mirror = mirror_fx.world();
    mirror.resize_grid(32, 32);
    ASSERT_EQ(0, mirror.new_specials) << "a fresh world starts classic";
    ASSERT_TRUE(og::sim::apply_snapshot(mirror, decoded));
    EXPECT_EQ(1, mirror.new_specials) << "the snapshot carries the setting";

    const walker* m_skel = mirror.find_by_id(skel_id);
    const walker* m_host = mirror.find_by_id(host_id);
    const walker* m_rider = mirror.find_by_id(rider_id);
    ASSERT_TRUE(m_skel && m_host && m_rider);
    ASSERT_NE(skel, m_skel) << "a different world's walkers";

    SeatTimer t = seat_timer(mirror, *m_skel);
    EXPECT_EQ("REASSEMBLE", label_of(t));
    EXPECT_EQ(123, t.ticks);
    t = seat_timer(mirror, *m_host);
    EXPECT_EQ("POSSESS", label_of(t));
    EXPECT_EQ(456, t.ticks);
    EXPECT_EQ(nullptr, seat_timer(mirror, *m_rider).label);
}

// og.C.MARKER_* and og.C.KIT_QUARTER_FREEZE carry the engine's numbers, so
// the Lua kits (packs/core/lib/kit_marker.lua reads its roles from them)
// and the C++ countdown can never disagree on a role.
//
// RED (run by hand): the MARKER_WARD row dropped from kKitConstants ->
// og.C.MARKER_WARD is nil and the chunk raises 'marker roles'.
namespace {

class TimedEffectsLua : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;

    void SetUp() override
    {
        init_all_registries();
        og::script::clear_pack_scripts();
    }
    void TearDown() override { og::script::clear_pack_scripts(); }
};

}  // namespace

TEST_F(TimedEffectsLua, the_marker_roles_reach_lua_with_the_engines_numbers)
{
    TestGameWorld tw;
    walker* self = tw.world().add_ob(Order::Living, FAMILY_SOLDIER);
    ASSERT_NE(nullptr, self);
    const auto n = [](int v) { return std::to_string(v); };
    const std::string body =
        "    local C = og.C\n"
        "    if C.MARKER_BURROW ~= " + n(MARKER_BURROW) + "\n"
        "        or C.MARKER_LEGION ~= " + n(MARKER_LEGION) + "\n"
        "        or C.MARKER_IMMOLATION ~= " + n(MARKER_IMMOLATION) + "\n"
        "        or C.MARKER_PHASE_VEIL ~= " + n(MARKER_PHASE_VEIL) + "\n"
        "        or C.MARKER_METEOR_RAIN ~= " + n(MARKER_METEOR_RAIN) + "\n"
        "        or C.MARKER_WARD ~= " + n(MARKER_WARD) + " then\n"
        "      error('marker roles')\n"
        "    end\n"
        "    if C.KIT_QUARTER_FREEZE ~= " + n(KIT_QUARTER_FREEZE) + " then\n"
        "      error('quarter freeze')\n"
        "    end";
    og::script::register_pack_script(
        {"test.kit", "kit_roles_probe.lua",
         "og.register_hooks('living', 'core:soldier', {\n"
         "  do_special = function(self)\n" +
             body +
             "\n    return true\n"
             "  end,\n"
             "})\n"});
    const auto handled = og::script::hooks::do_special(
        get_family_descriptor(FAMILY_SOLDIER), self);
    const auto& errors = og::script::active_world_scripts().host().errors();
    ASSERT_TRUE(handled.has_value())
        << (errors.empty() ? std::string("(no script error recorded)")
                           : errors.back().message);
    EXPECT_TRUE(*handled);
    EXPECT_EQ(6, MARKER_WARD) << "the ward's role byte";
    EXPECT_EQ(16, KIT_QUARTER_FREEZE) << "a free kit_state bit";
}
