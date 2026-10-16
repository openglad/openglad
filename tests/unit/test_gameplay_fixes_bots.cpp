/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

// The mage's and the archmage's bots with the Gameplay Fixes setting.
//
// In the classic game the mage's one bot rule ("cast when no foe is within
// 110 px, or when more than three are") was written for TELEPORT and applied
// to every slot, so a mage cast WARP SPACE, FREEZE TIME and ENERGY WAVE at
// nobody and held them back in a fight. The archmage had no rule at all for
// its first three slots, and its shift (heartburst or chain lightning, image
// or elemental) was a coin. With the setting on, each slot asks a question
// about its own reach, and the archmage picks the shift that has a target or
// that it can pay for. With the setting off every gate answers what it
// answered before, and the shift is left as the coin set it.
//
// The gates are asked directly (og::test::check_special_ai), the way
// living::check_special asks them after the coin; the hit response and the
// casts go through the real hooks and walker::special().

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
#include <openglad/gameplay/possession.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/resources/packs.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>

namespace {

int kit_marker_family()
{
    return og::families::resolve_family_string_id(Order::FX,
                                                  "core:kit_marker");
}

class GameplayFixesBots : public ::testing::Test {
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
        fresh_world();
    }

    void TearDown() override
    {
        EXPECT_EQ(0u, guard_.count()) << guard_.message();
        tw_.reset();
    }

    // An open field of grass, 1024 px a side, with nobody on it.
    void fresh_world()
    {
        tw_.reset();
        tw_ = std::make_unique<TestGameWorld>();
        GameWorld& w = world();
        w.resize_grid(64, 64);
        std::fill_n(w.grid.data.get(),
                    static_cast<std::size_t>(w.grid.w) * w.grid.h,
                    static_cast<std::uint8_t>(PIX_GRASS1));
        w.my_team = 0;
        w.gameplay_fixes = 0;
    }

    GameWorld& world() { return tw_->world(); }

    // A bot (ACT_RANDOM, no seat) with `mp` mana and no regeneration.
    living* add(int family, unsigned char team, short x, short y, int level,
                float mp = 0.0f)
    {
        walker* w = world().add_ob(Order::Living, family);
        if (w == nullptr)
            return nullptr;
        w->set_team_num(team);
        w->set_real_team_num(255);
        w->setxy(x, y);
        w->set_act_type(ACT_RANDOM);
        w->stats()->set_level(static_cast<short>(level));
        w->stats()->set_max_hitpoints(400.0f);
        w->stats()->set_hitpoints(400.0f);
        w->stats()->set_max_magicpoints(std::max(mp, 1.0f));
        w->stats()->set_magicpoints(mp);
        w->stats()->set_magic_per_round(0.0f);
        w->stats()->set_max_magic_delay(1000000);
        w->set_busy(0.0f);
        return dynamic_cast<living*>(w);
    }

    // The family's bot gate for `slot`, with the setting at `fixes`.
    bool gate(walker* w, int slot, short fixes)
    {
        world().gameplay_fixes = fixes;
        w->set_current_special(static_cast<char>(slot));
        return og::test::check_special_ai(*get_family_descriptor(w->family()),
                                          dynamic_cast<living*>(w));
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// The mage (fix B)
// ---------------------------------------------------------------------------

// TELEPORT keeps the classic rule on both values: flee when nobody is near
// or when more than three foes are.
//
// RED (the setting's slot-1 branch taken out): Value of: gate(mage, 1,
// fixes) Actual: false Expected: true "alone, setting 1".
TEST_F(GameplayFixesBots, mage_teleport_keeps_the_classic_rule_on_both_values)
{
    living* mage = add(FAMILY_MAGE, 1, 200, 200, 13, 2000.0f);
    ASSERT_NE(nullptr, mage);
    for (short fixes : {short{0}, short{1}})
        EXPECT_TRUE(gate(mage, 1, fixes)) << "alone, setting " << fixes;

    living* foe = add(FAMILY_SOLDIER, 2, 260, 200, 1);
    ASSERT_NE(nullptr, foe);
    for (short fixes : {short{0}, short{1}})
        EXPECT_FALSE(gate(mage, 1, fixes)) << "one foe, setting " << fixes;

    for (short dy : {short{20}, short{40}, short{60}})
        ASSERT_NE(nullptr, add(FAMILY_SOLDIER, 2, 230, static_cast<short>(200 + dy), 1));
    for (short fixes : {short{0}, short{1}})
        EXPECT_TRUE(gate(mage, 1, fixes)) << "four foes, setting " << fixes;
}

// WARP SPACE, FREEZE TIME and ENERGY WAVE: the classic gate cast them at
// nobody and held them in a fight. With the setting on they want a foe
// inside the same 110 px, and a crowd does not stop them.
//
// RED (the classic gate): Value of: gate(mage, slot, 1) Actual: true
// Expected: false "slot 2: nobody within 110"; and Actual: false Expected:
// true "slot 2: one foe at 60 px", the same for slots 3 and 4.
TEST_F(GameplayFixesBots, mage_slots_two_to_four_want_a_foe_within_110_with_the_setting_on)
{
    living* mage = add(FAMILY_MAGE, 1, 200, 200, 13, 2000.0f);
    living* far_foe = add(FAMILY_SOLDIER, 2, 311, 200, 1);  // 111 px
    ASSERT_TRUE(mage && far_foe);
    for (int slot = 2; slot <= 4; ++slot) {
        EXPECT_TRUE(gate(mage, slot, 0)) << "slot " << slot << ": the classic answer";
        EXPECT_FALSE(gate(mage, slot, 1)) << "slot " << slot << ": nobody within 110";
    }

    far_foe->setxy(310, 200);  // 110 px: the edge is in
    for (int slot = 2; slot <= 4; ++slot) {
        EXPECT_FALSE(gate(mage, slot, 0)) << "slot " << slot << ": the classic answer";
        EXPECT_TRUE(gate(mage, slot, 1)) << "slot " << slot << ": one foe at 110 px";
    }

    far_foe->setxy(260, 200);  // 60 px
    for (int slot = 2; slot <= 4; ++slot) {
        EXPECT_FALSE(gate(mage, slot, 0)) << "slot " << slot << ": the classic answer";
        EXPECT_TRUE(gate(mage, slot, 1)) << "slot " << slot << ": one foe at 60 px";
    }

    for (short dy : {short{20}, short{40}, short{60}})
        ASSERT_NE(nullptr, add(FAMILY_SOLDIER, 2, 230, static_cast<short>(200 + dy), 1));
    for (int slot = 2; slot <= 4; ++slot) {
        EXPECT_TRUE(gate(mage, slot, 0)) << "slot " << slot << ": the classic answer";
        EXPECT_TRUE(gate(mage, slot, 1)) << "slot " << slot << ": four foes";
    }
}

// HEARTBURST wants a foe inside its own reach, 80 + 2 px per level: 106 px
// at level 13, a little short of the teleport rule's 110.
//
// RED (the classic gate): Value of: gate(mage, 5, 1) Actual: true Expected:
// false "120 px: outside the burst"; Actual: false Expected: true "60 px".
TEST_F(GameplayFixesBots, mage_heartburst_wants_a_foe_in_its_own_reach_with_the_setting_on)
{
    living* mage = add(FAMILY_MAGE, 1, 200, 200, 13, 2000.0f);
    living* foe = add(FAMILY_SOLDIER, 2, 320, 200, 1);  // 120 px
    ASSERT_TRUE(mage && foe);
    EXPECT_TRUE(gate(mage, 5, 0)) << "120 px: the classic answer (nobody within 110)";
    EXPECT_FALSE(gate(mage, 5, 1)) << "120 px: outside the burst";

    foe->setxy(307, 200);  // 107 px: inside 110, outside 106
    EXPECT_FALSE(gate(mage, 5, 0)) << "107 px: the classic answer";
    EXPECT_FALSE(gate(mage, 5, 1)) << "107 px: one past the burst's reach";

    foe->setxy(306, 200);  // 106 px: the edge of the burst
    EXPECT_TRUE(gate(mage, 5, 1)) << "106 px: the edge is in";

    foe->setxy(260, 200);  // 60 px
    EXPECT_FALSE(gate(mage, 5, 0)) << "60 px: the classic answer";
    EXPECT_TRUE(gate(mage, 5, 1)) << "60 px";
}

// ---------------------------------------------------------------------------
// The archmage (fixes B and C)
// ---------------------------------------------------------------------------

// TELEPORT: the mage's rule with the setting on; the classic archmage had no
// gate, so every answer is yes with it off.
//
// RED (no gate on the row): Value of: gate(arch, 1, 1) Actual: true
// Expected: false "one foe at 60 px: stay and fight".
TEST_F(GameplayFixesBots, archmage_teleport_takes_the_mage_rule_with_the_setting_on)
{
    living* arch = add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 2000.0f);
    ASSERT_NE(nullptr, arch);
    EXPECT_TRUE(gate(arch, 1, 0));
    EXPECT_TRUE(gate(arch, 1, 1)) << "alone: flee";

    living* foe = add(FAMILY_SOLDIER, 2, 260, 200, 1);
    ASSERT_NE(nullptr, foe);
    EXPECT_TRUE(gate(arch, 1, 0)) << "the classic archmage had no gate";
    EXPECT_FALSE(gate(arch, 1, 1)) << "one foe at 60 px: stay and fight";

    foe->setxy(311, 200);  // 111 px: out of the rule's reach
    EXPECT_TRUE(gate(arch, 1, 1)) << "nobody within 110: flee";
    foe->setxy(310, 200);  // 110 px: the edge is in
    EXPECT_FALSE(gate(arch, 1, 1)) << "one foe at 110 px";

    for (short dy : {short{20}, short{40}, short{60}})
        ASSERT_NE(nullptr, add(FAMILY_SOLDIER, 2, 230, static_cast<short>(200 + dy), 1));
    EXPECT_TRUE(gate(arch, 1, 0));
    EXPECT_TRUE(gate(arch, 1, 1)) << "four foes: flee";
}

// HEARTBURST / CHAIN LIGHTNING at level 10: the burst reaches 80 + 2*10 =
// 100 px, the chain 200 + 5*10 + 2*10 = 270 px. The coin's arm is kept when
// it has a target, swapped when only the other one has, refused when
// neither has. The chain always reaches at least as far as the burst, so a
// chain with no target always ends in a refusal.
//
// RED (no gate on the row): Expected equality of these values: c.answer
// Which is: false gate(arch, 2, 1) Which is: true "271 px, the chain's
// coin: nobody in reach"; c.shift_after Which is: 1 arch->shifter_down()
// Which is: 0 "101 px: only the chain has a target".
TEST_F(GameplayFixesBots, archmage_burst_or_chain_takes_the_arm_with_a_target_with_the_setting_on)
{
    living* arch = add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 2000.0f);
    living* foe = add(FAMILY_SOLDIER, 2, 260, 200, 1);  // 60 px: both reach
    ASSERT_TRUE(arch && foe);

    struct Case {
        short x;
        short shift_before;
        bool answer;
        short shift_after;
        const char* what;
    };
    const Case cases[] = {
        {260, 0, true, 0, "60 px, the burst's coin: kept"},
        {260, 1, true, 1, "60 px, the chain's coin: kept"},
        {300, 0, true, 0, "100 px: the burst's edge is in"},
        {301, 0, true, 1, "101 px: only the chain has a target"},
        {400, 1, true, 1, "200 px, the chain's coin: kept"},
        {470, 0, true, 1, "270 px: the chain's edge is in"},
        {471, 1, false, 0, "271 px, the chain's coin: nobody in reach"},
        {471, 0, false, 0, "271 px, the burst's coin: nobody in reach"},
    };
    for (const Case& c : cases) {
        foe->setxy(c.x, static_cast<short>(200));
        arch->set_shifter_down(c.shift_before);
        EXPECT_EQ(c.answer, gate(arch, 2, 1)) << c.what;
        EXPECT_EQ(c.shift_after, arch->shifter_down()) << c.what;
    }

    // With the setting off the row has no opinion and the coin stands.
    foe->setxy(471, 200);
    arch->set_shifter_down(1);
    EXPECT_TRUE(gate(arch, 2, 0)) << "the classic archmage had no gate";
    EXPECT_EQ(1, arch->shifter_down()) << "the coin is left as it fell";
}

// SUMMON IMAGE / SUMMON ELEMENTAL: a foe within 200 px, and the elemental
// only when the spare pool (magic less the slot's 500) is at least 500, so
// a drained archmage summons an image rather than spend the last of its
// magic on an elemental.
//
// RED (no gate on the row): Value of: gate(arch, 3, 1) Actual: true
// Expected: false "201 px: nobody to summon against"; Expected equality of
// these values: c.shift_after Which is: 0 arch->shifter_down() Which is: 1
// "spare 100: the image".
TEST_F(GameplayFixesBots, archmage_summon_wants_a_foe_and_the_pool_for_an_elemental_with_the_setting_on)
{
    living* arch = add(FAMILY_ARCHMAGE, 1, 200, 200, 10, 600.0f);
    living* foe = add(FAMILY_SOLDIER, 2, 350, 200, 1);  // 150 px
    ASSERT_TRUE(arch && foe);
    ASSERT_EQ(500, static_cast<int>(arch->stats()->special_cost(3))) << "the slot's price";

    struct Case {
        float mp;
        short shift_before;
        short shift_after;
        const char* what;
    };
    const Case cases[] = {
        {600.0f, 1, 0, "spare 100: the image"},
        {999.0f, 1, 0, "spare 499: the image"},
        {1000.0f, 0, 1, "spare 500: the elemental"},
        {1100.0f, 0, 1, "spare 600: the elemental"},
    };
    for (const Case& c : cases) {
        arch->stats()->set_magicpoints(c.mp);
        arch->set_shifter_down(c.shift_before);
        EXPECT_TRUE(gate(arch, 3, 1)) << c.what << ": a foe at 150 px";
        EXPECT_EQ(c.shift_after, arch->shifter_down()) << c.what;
    }

    foe->setxy(400, 200);  // 200 px: the edge is in
    EXPECT_TRUE(gate(arch, 3, 1)) << "200 px";
    foe->setxy(401, 200);  // 201 px
    EXPECT_FALSE(gate(arch, 3, 1)) << "201 px: nobody to summon against";

    // With the setting off the row has no opinion and the coin stands.
    arch->stats()->set_magicpoints(600.0f);
    arch->set_shifter_down(1);
    EXPECT_TRUE(gate(arch, 3, 0)) << "the classic archmage had no gate";
    EXPECT_EQ(1, arch->shifter_down()) << "the coin is left as it fell";
}

// ---------------------------------------------------------------------------
// The archmage's hit response (fix C)
// ---------------------------------------------------------------------------

namespace {

struct SummonOutcome {
    int elementals = 0;
    int phantoms = 0;
};

SummonOutcome summons_of(GameWorld& w, const walker* owner)
{
    SummonOutcome out;
    for (const auto& ob : w.oblist) {
        if (ob == nullptr || ob->dead() != 0 || ob->owner() != owner)
            continue;
        if (ob->query_order() != Order::Living)
            continue;
        if (std::string(ob->stats()->name) == "Phantom")
            out.phantoms++;
        else if (ob->family() == FAMILY_FIREELEMENTAL)
            out.elementals++;
    }
    return out;
}

class GameplayFixesArchmageHit : public GameplayFixesBots {
protected:
    // A healthy level-10 archmage on team 1 is struck by an orc 30 px away.
    // Its shift is `shift` (what the last coin left, or a player's Shift
    // key), its magic `mp`; `seated` makes it a hero a seat drives.
    SummonOutcome struck(short fixes, short shift, float mp, bool seated)
    {
        fresh_world();
        world().gameplay_fixes = fixes;
        living* arch = add(FAMILY_ARCHMAGE, 1, 200, 200, 10, mp);
        living* orc = add(FAMILY_ORC, 2, 230, 200, 1);
        EXPECT_TRUE(arch && orc);
        if (!(arch && orc))
            return {};
        orc->stats()->set_max_hitpoints(900.0f);
        orc->stats()->set_hitpoints(900.0f);
        if (seated) {
            arch->set_user(0);
            arch->set_act_type(ACT_CONTROL);
        }
        EXPECT_EQ(seated, og::sim::seat_driven(*arch));
        arch->set_shifter_down(shift);
        og::test::hit_response(*get_family_descriptor(FAMILY_ARCHMAGE),
                               arch->stats(), orc);
        return summons_of(world(), arch);
    }
};

}  // namespace

// A struck bot archmage answers with a summon. In the classic game the
// summon's shift was whatever the last coin left, so a drained archmage
// could spend the last of its magic on an elemental. With the setting on a
// bot decides by its pool, as its summon gate does.
//
// RED (the classic hit response): Expected equality of these values: 0
// on.elementals Which is: 1 "spare 100 with the setting on: the image".
TEST_F(GameplayFixesArchmageHit, a_struck_bot_summons_by_its_pool_not_the_stale_coin)
{
    const SummonOutcome off = struck(0, 1, 600.0f, false);
    EXPECT_EQ(1, off.elementals) << "the classic game: the stale coin summons the elemental";
    EXPECT_EQ(0, off.phantoms);

    const SummonOutcome on = struck(1, 1, 600.0f, false);
    EXPECT_EQ(0, on.elementals) << "spare 100 with the setting on: the image";
    EXPECT_EQ(1, on.phantoms);

    const SummonOutcome rich = struck(1, 0, 1100.0f, false);
    EXPECT_EQ(1, rich.elementals) << "spare 600 with the setting on: the elemental";
    EXPECT_EQ(0, rich.phantoms);
}

// The hit response runs for a player's archmage too, and a player's shift is
// the Shift key: the setting leaves it alone, so a seated archmage summons
// the same thing on both values.
//
// RED (the seat test taken out of the hit response): Expected equality of
// these values: 1 up.phantoms Which is: 0 "Shift up, spare 600, setting 1";
// and 1 held.elementals Which is: 0 "Shift held, spare 100, setting 1".
TEST_F(GameplayFixesArchmageHit, a_seated_archmage_keeps_its_shift_key)
{
    for (short fixes : {short{0}, short{1}}) {
        const SummonOutcome up = struck(fixes, 0, 1100.0f, true);
        EXPECT_EQ(1, up.phantoms) << "Shift up, spare 600, setting " << fixes;
        EXPECT_EQ(0, up.elementals) << "Shift up, spare 600, setting " << fixes;

        const SummonOutcome held = struck(fixes, 1, 600.0f, true);
        EXPECT_EQ(1, held.elementals) << "Shift held, spare 100, setting " << fixes;
        EXPECT_EQ(0, held.phantoms) << "Shift held, spare 100, setting " << fixes;
    }
}

// ---------------------------------------------------------------------------
// MIND CONTROL and a seated hero (#355)
// ---------------------------------------------------------------------------

namespace {

// The first world-RNG state from which the next draw of next(bound) is
// `want`: pins the domination roll without a spy.
std::uint32_t state_where_next_is(std::uint32_t bound, std::uint32_t want)
{
    for (std::uint32_t seed = 1;; ++seed) {
        og::sim::SimRandom probe(seed);
        if (probe.next(bound) == want)
            return seed;
    }
}

struct MindControlCast {
    bool ok = false;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
};

}  // namespace

// An enemy archmage's MIND CONTROL takes a hero a player is driving in the
// classic game. With the setting on the hero resists, as it does Possess
// ("HERO RESISTS"): it is skipped before the roll, costs the cast nothing,
// and an NPC beside it is still taken.
//
// RED (the classic eligibility): Value of: c.ok Actual: true Expected: false
// "the hero alone"; Expected equality of these values: 0
// static_cast<int>(hero->team_num()) Which is: 2; mp_before Which is: 1000
// arch->stats()->magicpoints() Which is: 850 "a refusal spends nothing".
TEST_F(GameplayFixesBots, mind_control_skips_a_seated_hero_with_the_setting_on)
{
    // The domination roll (rand(20) != 0) succeeds on every cast below.
    const std::uint32_t roll = state_where_next_is(20, 1);
    auto scene = [&](short fixes, bool with_npc, walker** hero_out,
                     walker** npc_out, walker** arch_out) {
        fresh_world();
        world().gameplay_fixes = fixes;
        living* arch = add(FAMILY_ARCHMAGE, 2, 200, 200, 10, 1000.0f);
        living* hero = add(FAMILY_SOLDIER, 0, 230, 200, 1);
        ASSERT_TRUE(arch && hero);
        hero->set_user(0);
        hero->set_act_type(ACT_CONTROL);
        ASSERT_TRUE(og::sim::seat_driven(*hero));
        living* npc = nullptr;
        if (with_npc) {
            npc = add(FAMILY_SOLDIER, 0, 200, 240, 1);
            ASSERT_NE(nullptr, npc);
            ASSERT_FALSE(og::sim::seat_driven(*npc));
        }
        world().rng_.state_ = roll;
        *hero_out = hero;
        *npc_out = npc;
        *arch_out = arch;
    };
    auto cast = [](walker* arch) {
        arch->set_current_special(4);
        arch->set_shifter_down(0);
        MindControlCast c;
        c.ok = arch->special(&c.why, &c.reason);
        return c;
    };

    walker* hero = nullptr;
    walker* npc = nullptr;
    walker* arch = nullptr;

    scene(0, false, &hero, &npc, &arch);
    ASSERT_FALSE(HasFatalFailure());
    MindControlCast c = cast(arch);
    EXPECT_TRUE(c.ok) << "the classic game takes the hero: " << c.reason;
    EXPECT_EQ(2, static_cast<int>(hero->team_num()));
    EXPECT_EQ(0, static_cast<int>(hero->real_team_num()));

    scene(1, false, &hero, &npc, &arch);
    ASSERT_FALSE(HasFatalFailure());
    const float mp_before = arch->stats()->magicpoints();
    c = cast(arch);
    EXPECT_FALSE(c.ok) << "the hero alone";
    EXPECT_EQ(walker::SpecialFailure::ScriptDeclined, c.why);
    EXPECT_EQ("NO VALID TARGET", c.reason);
    EXPECT_EQ(0, static_cast<int>(hero->team_num()));
    EXPECT_EQ(255, static_cast<int>(hero->real_team_num()));
    EXPECT_EQ(0, hero->charm_left());
    EXPECT_FLOAT_EQ(mp_before, arch->stats()->magicpoints()) << "a refusal spends nothing";
    EXPECT_EQ(0, arch->special_rest_left()) << "and starts no rest";
    EXPECT_EQ(roll, world().rng_.state_) << "the hero is skipped before the roll";

    scene(1, true, &hero, &npc, &arch);
    ASSERT_FALSE(HasFatalFailure());
    c = cast(arch);
    EXPECT_TRUE(c.ok) << c.reason;
    EXPECT_EQ(0, static_cast<int>(hero->team_num())) << "the hero resists";
    EXPECT_EQ(255, static_cast<int>(hero->real_team_num()));
    EXPECT_EQ(2, static_cast<int>(npc->team_num())) << "the NPC beside it is taken";
    EXPECT_EQ(0, static_cast<int>(npc->real_team_num()));
}
