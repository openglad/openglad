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
