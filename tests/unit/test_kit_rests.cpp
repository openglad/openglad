/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// Rests: the specials that upset a whole fight -- GLIMMER, FREEZE TIME,
// HOWL (orc and captain), SCARE / WAIL and MIND CONTROL -- wait between
// casts, without holding up ordinary attacks. FREEZE TIME, HOWL, SCARE and
// MIND CONTROL rest whatever the New Specials setting says; GLIMMER, WAIL
// and the captain's HOWL exist only with it on. A rest is a clock on the
// caster (special_rest_left ticks for the 1-based slot special_rest_slot),
// counted down by living::act, started by one wrapper
// (lib/living_common.lua with_rest) and read by the bots' gates
// (gate_with_rest). The refusal names the seconds left through
// og.timer_seconds, the HUD's own rounding.
//
// Every cast goes through the real walker::special() (the price gate and the
// charge included). Each special's test: the first cast lands, an immediate
// second is refused with the exact text and spends nothing, one tick before
// the rest is over it still refuses ("IN 0S"), and on the last tick it casts
// again.
//
// RED (each test below was first run against the kits without rests, or
// against the rule it pins put back the old way; the lines it printed are
// quoted beside it).

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
#include <openglad/gameplay/living.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Cast {
    bool ok = false;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
};

Cast cast(walker* w, int slot, short shift = 0)
{
    w->set_current_special(static_cast<char>(slot));
    w->set_shifter_down(shift);
    Cast out;
    out.ok = w->special(&out.why, &out.reason);
    return out;
}

int kit_marker_family()
{
    return og::families::resolve_family_string_id(Order::FX,
                                                  "core:kit_marker");
}

class KitRests : public ::testing::Test {
protected:
    og::test::ScopedPackStoreState pack_store_restore_;
    std::unique_ptr<TestGameWorld> tw_;
    og::test::ScopedHookFailureGuard guard_;

    void SetUp() override
    {
        og::test::mount_core_pack();
        // A test elsewhere in the binary may have reset the mod slots that
        // hold the kits' families (the KitSkeleton fixture's rule).
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

    void tick(int n = 1)
    {
        for (int i = 0; i < n; ++i)
            world().tick();
    }

    // A walker that takes no decisions of its own (ACT_CONTROL with no seat)
    // with `mp` mana and no regeneration, so every MP change is the cast's.
    living* add(int family, unsigned char team, short x, short y, int level,
                float mp = 0.0f)
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
        w->stats()->set_level(static_cast<short>(level));
        w->stats()->set_max_hitpoints(200.0f);
        w->stats()->set_hitpoints(200.0f);
        w->stats()->set_max_magicpoints(mp);
        w->stats()->set_magicpoints(mp);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_max_magic_delay(1000000);
        w->stats()->set_heal_per_round(0.0f);
        w->set_lastx(1);
        w->set_lasty(0);
        return dynamic_cast<living*>(w);
    }

    // The caster's rest clock: ticks left, and the slot that rests.
    static int rest_left(const walker* w) { return w->special_rest_left(); }
    static int rest_slot(const walker* w) { return w->special_rest_slot(); }

    float mp(const walker* w) const { return w->stats()->magicpoints(); }

    bool gate(walker* w, int slot)
    {
        w->set_current_special(static_cast<char>(slot));
        return og::test::check_special_ai(*get_family_descriptor(w->family()),
                                          dynamic_cast<living*>(w));
    }

    // The shared shape: cast lands; an immediate press is refused with
    // `refusal` for free; one tick before the rest's end it reads 0S; on the
    // last tick it casts again. `rest` is the rest in ticks.
    // `foe`, when given, is walked back beside the caster before the last
    // cast (a frightened foe runs off during the rest).
    void expect_rested(walker* w, int slot, short shift, int rest,
                       const std::string& refusal, const std::string& name,
                       walker* foe = nullptr)
    {
        const Cast first = cast(w, slot, shift);
        ASSERT_TRUE(first.ok) << first.reason;
        EXPECT_EQ(rest, rest_left(w)) << "the cast starts the rest";
        EXPECT_EQ(slot, rest_slot(w)) << "the rested slot";

        const float before = mp(w);
        const Cast again = cast(w, slot, shift);
        EXPECT_FALSE(again.ok);
        EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, again.why);
        EXPECT_EQ(refusal, again.reason);
        EXPECT_LE(again.reason.size(), 24u) << "fits the compact HUD";
        EXPECT_FLOAT_EQ(before, mp(w)) << "a refusal spends nothing";
        EXPECT_EQ(rest, rest_left(w)) << "and does not restart the rest";

        tick(rest - 1);
        EXPECT_EQ(1, rest_left(w)) << "one tick a tick";
        w->set_busy(0.0f);
        const Cast late = cast(w, slot, shift);
        EXPECT_FALSE(late.ok) << "one tick of rest left";
        EXPECT_EQ(name + " IN 0S", late.reason);

        tick();
        EXPECT_EQ(0, rest_left(w)) << "the rest is over";
        if (foe != nullptr)
            foe->setxy(static_cast<short>(w->xpos() + 30), w->ypos());
        w->set_busy(0.0f);
        const Cast rested = cast(w, slot, shift);
        EXPECT_TRUE(rested.ok) << rested.reason;
        EXPECT_EQ(rest, rest_left(w)) << "and a new one starts";
    }
};

}  // namespace

// FREEZE TIME: 360 ticks (30 s). A mage on the player's team stops time;
// the second press is refused, and the world's time-stop bank is untouched.
//
// RED: Expected equality of these values: "FREEZE TIME IN 30S"
// again.reason Which is: "" (it cast again), and world().enemy_freeze
// Which is: 194 against the bank's 97.
TEST_F(KitRests, freeze_time_rests_thirty_seconds)
{
    living* mage = add(FAMILY_MAGE, 0, 160, 160, 7, 2000.0f);
    ASSERT_NE(nullptr, mage);
    const Cast first = cast(mage, 3);
    ASSERT_TRUE(first.ok) << first.reason;
    const int bank = world().enemy_freeze;
    ASSERT_GT(bank, 0) << "time stands still";
    const Cast again = cast(mage, 3);
    EXPECT_EQ("FREEZE TIME IN 30S", again.reason);
    EXPECT_EQ(bank, world().enemy_freeze) << "a refused cast banks nothing";
    EXPECT_FLOAT_EQ(1500.0f, mp(mage));

    // The shared shape on a fresh mage.
    world().enemy_freeze = 0;
    living* other = add(FAMILY_MAGE, 0, 200, 160, 7, 2000.0f);
    ASSERT_NE(nullptr, other);
    expect_rested(other, 3, 0, 360, "FREEZE TIME IN 30S", "FREEZE TIME");
}

// GLIMMER: 180 ticks (15 s), and it now costs 40.
//
// RED: Expected equality of these values: 360.0f mp(faerie) Which is: 376
// (the old price, 24), and "GLIMMER IN 15S" again.reason Which is:
// "SPECIAL BUSY"
TEST_F(KitRests, glimmer_costs_forty_and_rests_fifteen_seconds)
{
    living* faerie = add(FAMILY_FAERIE, 0, 160, 160, 4, 400.0f);
    ASSERT_NE(nullptr, faerie);
    faerie->set_busy(0.0f);
    ASSERT_TRUE(cast(faerie, 2).ok);
    EXPECT_FLOAT_EQ(360.0f, mp(faerie)) << "GLIMMER costs 40 (its shots pre-paid)";
    const Cast again = cast(faerie, 2);
    EXPECT_EQ("GLIMMER IN 15S", again.reason);
    EXPECT_FLOAT_EQ(360.0f, mp(faerie));

    living* other = add(FAMILY_FAERIE, 0, 100, 100, 4, 400.0f);
    ASSERT_NE(nullptr, other);
    other->set_busy(0.0f);
    expect_rested(other, 2, 0, 180, "GLIMMER IN 15S", "GLIMMER");
}

// HOWL, the orc's: 120 ticks (10 s). Refused before the attack pause it
// would otherwise answer (SPECIAL BUSY).
//
// RED (against the kits without rests, when the rest was a kit marker):
// Expected equality of these values: 1u started.size() Which is: 0 (no
// rest; the captain's test below printed the same). Against the clock with
// its decrement taken out: "HOWL IN 0S" late.reason Which is: "HOWL IN 10S".
TEST_F(KitRests, orc_howl_rests_ten_seconds)
{
    living* orc = add(FAMILY_ORC, 0, 160, 160, 4, 400.0f);
    living* foe = add(FAMILY_SOLDIER, 1, 200, 160, 1);
    ASSERT_TRUE(orc && foe);
    expect_rested(orc, 1, 0, 120, "HOWL IN 10S", "HOWL");
}

// HOWL, the captain's: the same rest, read from the captain's own tuning.
TEST_F(KitRests, captain_howl_rests_ten_seconds)
{
    living* captain = add(FAMILY_BIG_ORC, 0, 160, 160, 1, 400.0f);
    ASSERT_NE(nullptr, captain);
    expect_rested(captain, 1, 0, 120, "HOWL IN 10S", "HOWL");
}

// SCARE and WAIL share one rest of 96 ticks (8 s); each refuses in its own
// name, and a SCARE rests the WAIL too.
//
// RED (against the kits without rests, when the rest was a kit marker):
// Expected equality of these values: 1u started.size() Which is: 0, and
// Value of: wail.ok Actual: true Expected: false (a WAIL at once).
TEST_F(KitRests, scare_and_wail_share_an_eight_second_rest)
{
    living* ghost = add(FAMILY_GHOST, 0, 160, 160, 4, 400.0f);
    living* foe = add(FAMILY_SOLDIER, 1, 190, 160, 1);
    ASSERT_TRUE(ghost && foe);
    expect_rested(ghost, 1, 0, 96, "SCARE IN 8S", "SCARE", foe);

    // The rest the last SCARE started holds the WAIL.
    const float before = mp(ghost);
    const Cast wail = cast(ghost, 1, 1);
    EXPECT_FALSE(wail.ok);
    EXPECT_EQ("WAIL IN 8S", wail.reason);
    EXPECT_FLOAT_EQ(before, mp(ghost));

    living* other = add(FAMILY_GHOST, 0, 100, 100, 4, 400.0f);
    living* near = add(FAMILY_SOLDIER, 1, 130, 100, 1);
    ASSERT_TRUE(other && near);
    expect_rested(other, 1, 1, 96, "WAIL IN 8S", "WAIL", near);
}

// MIND CONTROL: 240 ticks (20 s). A fresh foe walks up for the cast after
// the rest (the first two are still charmed).
//
// RED: Expected equality of these values: "MIND CONTROL IN 20S"
// again.reason Which is: "SPECIAL BUSY"
TEST_F(KitRests, mind_control_rests_twenty_seconds)
{
    living* arch = add(FAMILY_ARCHMAGE, 0, 160, 160, 10, 1000.0f);
    living* foe = add(FAMILY_SOLDIER, 1, 200, 160, 1);
    ASSERT_TRUE(arch && foe);
    const Cast first = cast(arch, 4);
    ASSERT_TRUE(first.ok) << first.reason;
    EXPECT_NE(1, foe->team_num()) << "the foe is charmed away";
    const float before = mp(arch);
    const Cast again = cast(arch, 4);
    EXPECT_FALSE(again.ok);
    EXPECT_EQ("MIND CONTROL IN 20S", again.reason);
    EXPECT_FLOAT_EQ(before, mp(arch));

    tick(239);
    arch->set_busy(0.0f);
    EXPECT_EQ("MIND CONTROL IN 0S", cast(arch, 4).reason);
    tick();
    arch->set_busy(0.0f);
    living* fresh = add(FAMILY_SOLDIER, 1, 120, 160, 1);
    ASSERT_NE(nullptr, fresh);
    const Cast rested = cast(arch, 4);
    EXPECT_TRUE(rested.ok) << rested.reason;
    EXPECT_NE(1, fresh->team_num()) << "the fresh foe is charmed";
}

// With the setting off FREEZE TIME, HOWL, SCARE and MIND CONTROL still
// rest (the maintainer's rule: the classic specials' rests are not the
// setting's), each refusing in its own name for free, and the clock counts
// down the same; GLIMMER and the captain's HOWL are not specials at all with
// it off, so they start no rest.
//
// RED (the setting check put back at the head of with_rest in the staged
// living_common.lua, the classic cast answering alone): Expected equality of
// these values: 360 rest_left(w) Which is: 0; Value of: again.ok Actual:
// true Expected: false; "FREEZE TIME IN 30S" again.reason Which is: "", and
// the same for HOWL, SCARE and MIND CONTROL.
TEST_F(KitRests, with_the_setting_off_the_classic_specials_rest_too)
{
    world().new_specials = 0;
    struct Row {
        int family;
        int slot;
        int level;
        int rest;
        const char* refusal;
        const char* name;
    };
    const Row rows[] = {
        {FAMILY_MAGE, 3, 7, 360, "FREEZE TIME IN 30S", "mage FREEZE TIME"},
        {FAMILY_ORC, 1, 4, 120, "HOWL IN 10S", "orc HOWL"},
        {FAMILY_GHOST, 1, 4, 96, "SCARE IN 8S", "ghost SCARE"},
        {FAMILY_ARCHMAGE, 4, 10, 240, "MIND CONTROL IN 20S",
         "archmage MIND CONTROL"},
    };
    short x = 60;
    for (const Row& row : rows) {
        living* w = add(row.family, 0, x, 60, row.level, 2000.0f);
        living* foe = add(FAMILY_SOLDIER, 1, static_cast<short>(x + 30), 60, 1);
        ASSERT_TRUE(w && foe) << row.name;
        w->set_busy(0.0f);
        const Cast first = cast(w, row.slot);
        ASSERT_TRUE(first.ok) << row.name << ": " << first.reason;
        EXPECT_EQ(row.rest, rest_left(w)) << row.name;
        EXPECT_EQ(row.slot, rest_slot(w)) << row.name;
        world().enemy_freeze = 0;
        w->set_busy(0.0f);
        const float before = mp(w);
        const Cast again = cast(w, row.slot);
        EXPECT_FALSE(again.ok) << row.name;
        EXPECT_EQ(row.refusal, again.reason) << row.name;
        EXPECT_FLOAT_EQ(before, mp(w)) << row.name << ": a refusal is free";
        tick(10);
        EXPECT_EQ(row.rest - 10, rest_left(w)) << row.name << ": it counts down";
        w->set_dead(1);
        foe->set_dead(1);
        x = static_cast<short>(x + 70);
    }

    living* faerie = add(FAMILY_FAERIE, 0, 260, 260, 4, 400.0f);
    living* captain = add(FAMILY_BIG_ORC, 0, 160, 260, 1, 400.0f);
    ASSERT_TRUE(faerie && captain);
    EXPECT_FALSE(cast(faerie, 2).ok) << "no GLIMMER with the setting off";
    EXPECT_FALSE(cast(captain, 1).ok) << "no captain's HOWL with it off";
    EXPECT_EQ(0, rest_left(faerie));
    EXPECT_EQ(0, rest_left(captain));
}

// The bots: while a special rests, its gate answers no, so a bot does not
// roll a slot it would be refused; once the rest is over the gate answers
// as before. A family-wide gate (the mage's, the orc's) holds only the
// rested slot. The classic specials' gates hold with the setting off too.
// With the Gameplay Fixes setting on, the mage's FREEZE TIME wants a foe
// within 110 px, so that row puts one at 60 px: the rest is still what is
// tested.
//
// RED: Value of: gate(w, row.slot) Actual: true Expected: false
// "mage FREEZE TIME: held while resting", and the same for every row. RED
// (the setting check put back in gate_with_rest): the same line for every
// setting-off row ("mage FREEZE TIME, setting off: held while resting").
// RED (the Gameplay Fixes row on the classic mage gate): Value of: gate(w,
// row.slot) Actual: false Expected: true "mage FREEZE TIME, Gameplay Fixes
// on: the classic gate says yes".
TEST_F(KitRests, bot_gates_hold_a_resting_special)
{
    struct Row {
        int family;
        int slot;
        short shift;
        int level;
        int rest;
        int new_specials;
        const char* name;
        short gameplay_fixes = 0;
    };
    const Row rows[] = {
        {FAMILY_MAGE, 3, 0, 7, 360, 1, "mage FREEZE TIME"},
        {FAMILY_FAERIE, 2, 0, 4, 180, 1, "faerie GLIMMER"},
        {FAMILY_ORC, 1, 0, 4, 120, 1, "orc HOWL"},
        {FAMILY_BIG_ORC, 1, 0, 1, 120, 1, "captain HOWL"},
        {FAMILY_GHOST, 1, 0, 4, 96, 1, "ghost SCARE"},
        {FAMILY_ARCHMAGE, 4, 0, 10, 240, 1, "archmage MIND CONTROL"},
        {FAMILY_MAGE, 3, 0, 7, 360, 0, "mage FREEZE TIME, setting off"},
        {FAMILY_ORC, 1, 0, 4, 120, 0, "orc HOWL, setting off"},
        {FAMILY_GHOST, 1, 0, 4, 96, 0, "ghost SCARE, setting off"},
        {FAMILY_ARCHMAGE, 4, 0, 10, 240, 0, "archmage MIND CONTROL, setting off"},
        {FAMILY_MAGE, 3, 0, 7, 360, 1, "mage FREEZE TIME, Gameplay Fixes on", 1},
    };
    short x = 40;
    for (const Row& row : rows) {
        world().new_specials = static_cast<short>(row.new_specials);
        world().gameplay_fixes = row.gameplay_fixes;
        living* w = add(row.family, 0, x, 120, row.level, 2000.0f);
        // A foe in every gate's reach (the mage's classic gate wants none or
        // a crowd: it stands off to one side for the mage; with the Gameplay
        // Fixes setting on, FREEZE TIME wants one within 110 px).
        const bool mage_off_to_one_side =
            row.family == FAMILY_MAGE && row.gameplay_fixes == 0;
        short foe_x = static_cast<short>(x + 30);
        if (mage_off_to_one_side)
            foe_x = x;
        else if (row.family == FAMILY_MAGE)
            foe_x = static_cast<short>(x + 60);
        living* foe = add(FAMILY_SOLDIER, 1, foe_x,
                          mage_off_to_one_side ? 400 : 120, 1);
        ASSERT_TRUE(w && foe) << row.name;
        w->set_busy(0.0f);
        ASSERT_TRUE(gate(w, row.slot)) << row.name << ": the classic gate says yes";
        ASSERT_TRUE(cast(w, row.slot, row.shift).ok) << row.name;
        world().enemy_freeze = 0;
        EXPECT_FALSE(gate(w, row.slot)) << row.name << ": held while resting";
        if (row.family == FAMILY_MAGE || row.family == FAMILY_ORC) {
            // With the Gameplay Fixes setting on, the mage's TELEPORT keeps
            // the classic rule (one foe near: stay), so WARP SPACE is asked.
            int other = row.slot == 1 ? 2 : 1;
            if (row.gameplay_fixes != 0)
                other = 2;
            EXPECT_TRUE(gate(w, other))
                << row.name << ": another slot is not held";
        }
        tick(row.rest);
        EXPECT_EQ(0, rest_left(w)) << row.name;
        world().enemy_freeze = 0;
        if (row.family != FAMILY_MAGE)  // the scared and howled-at walk off
            foe->setxy(static_cast<short>(w->xpos() + 30), w->ypos());
        EXPECT_TRUE(gate(w, row.slot)) << row.name << ": free after the rest";
        x = static_cast<short>(x + 30);
        w->set_dead(1);
        foe->set_dead(1);
    }
}

// The clock is the caster's own: no helper entity is summoned, it counts
// down on every turn the caster takes -- a frozen caster's included, since
// the freeze is checked later in the turn -- and it waits while time stands
// still for the caster's side, when the caster takes no turn at all. One
// clock per walker: a rest on one slot leaves the others free.
//
// RED (with the decrement taken out of living::act): Expected equality of
// these values: 0 rest_left(frozen) Which is: 120 "a frozen orc's rest ran
// out", and 0 rest_left(enemy) Which is: 120 "and runs once time moves
// again"; every expect_rested test printed "FREEZE TIME IN 0S" again.reason
// Which is: "FREEZE TIME IN 30S" the same way.
TEST_F(KitRests, the_clock_is_on_the_walker)
{
    living* frozen = add(FAMILY_ORC, 0, 60, 60, 4, 400.0f);
    living* enemy = add(FAMILY_ORC, 1, 200, 200, 4, 400.0f);
    ASSERT_TRUE(frozen && enemy);
    const std::size_t obs_before = world().oblist.size();
    ASSERT_TRUE(cast(frozen, 1).ok);
    ASSERT_TRUE(cast(enemy, 1).ok);
    EXPECT_EQ(obs_before, world().oblist.size()) << "no helper entity";
    EXPECT_EQ(120, rest_left(frozen));
    EXPECT_EQ(1, rest_slot(frozen));
    frozen->stats()->set_frozen_delay(500);
    world().enemy_freeze = 500;  // time stands still for team 1
    tick(120);
    EXPECT_EQ(0, rest_left(frozen)) << "a frozen orc's rest ran out";
    EXPECT_EQ(120, rest_left(enemy))
        << "the time-stopped orc took no turn, so its rest waits";
    world().enemy_freeze = 0;
    tick(120);
    EXPECT_EQ(0, rest_left(enemy)) << "and runs once time moves again";

    // Another slot is not held: the orc eats with HOWL resting.
    living* orc = add(FAMILY_ORC, 0, 120, 60, 4, 400.0f);
    ASSERT_NE(nullptr, orc);
    ASSERT_TRUE(cast(orc, 1).ok);
    orc->set_busy(0.0f);
    const Cast eat = cast(orc, 2);
    EXPECT_EQ(std::string::npos, eat.reason.find(" IN "))
        << "EAT CORPSE is not refused by HOWL's rest: " << eat.reason;
    EXPECT_EQ(1, rest_slot(orc)) << "the clock still names HOWL";
}
