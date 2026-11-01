/* Copyright (C) 1995-2002  FSGames. Ported by Sean Ford and Yan Shosh
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#pragma once

// The GameplayFixesBots fixture: an open field, bots placed by hand, and a
// family's bot gate asked directly (og::test::check_special_ai), the way
// living::check_special asks it after the Shift coin.
//
// Several translation units of one test binary share the suite name
// GameplayFixesBots (test_gameplay_fixes_bots.cpp and the bot-gate tests
// beside it), and GoogleTest requires every test of a suite to use the one
// fixture TYPE. So the class lives here, in a named namespace, with every
// member defined in the class (inline): one type in every file that
// includes it. A file uses it with
//     using og::test::bot_gates::GameplayFixesBots;

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
#include <cstddef>
#include <cstdint>
#include <memory>

namespace og::test::bot_gates {

inline int kit_marker_family()
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

    // A kit's bot gate for `slot`: the kit arms play with New Specials on
    // (`new_specials` 0 asks the classic kit), the Gameplay Fixes setting
    // left where the test put it.
    bool kit_gate(walker* w, int slot, short new_specials = 1)
    {
        world().new_specials = new_specials;
        w->set_current_special(static_cast<char>(slot));
        return og::test::check_special_ai(*get_family_descriptor(w->family()),
                                          dynamic_cast<living*>(w));
    }

    // The body a blow is measured against. The fixture does not promise a
    // sprite, so the tests set the sizes the sprites really have: the common
    // fighter is 16 by 16 (pix/footman.json, pix/orc.json, pix/mage.json),
    // the tower 25 by 29 (pix/towersm1.json).
    template <class W>
    static W* body(W* w, short sx, short sy)
    {
        if (w != nullptr) {
            w->set_sizex(sx);
            w->set_sizey(sy);
        }
        return w;
    }

    // A wall cell at grid (tx, ty): what the quiet probes of CHARGE's front
    // and path and every weapon probe on a pad meet on a level.
    static void pad_wall(GameWorld& w, int tx, int ty)
    {
        w.grid.data[static_cast<std::size_t>(tx + w.grid.w * ty)] =
            static_cast<std::uint8_t>(PIX_H_WALL1);
    }

    // The obmap files a body under every cell its box covers when it moves,
    // so a body whose size the test set after it was placed is moved once
    // more: the quiet probes and the pad probe read the obmap, not positions.
    template <class W>
    static W* settle_body(W* w)
    {
        if (w != nullptr) {
            const short x = w->xpos();
            const short y = w->ypos();
            w->setxy(static_cast<short>(x + 1), y);
            w->setxy(x, y);
        }
        return w;
    }
};

}  // namespace og::test::bot_gates
