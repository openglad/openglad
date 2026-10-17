// The elf's rock volleys with the Gameplay Fixes setting
// (packs/core/families/living-01-elf.lua). A volley pre-refunds weapon costs
// so that the rocks it throws are paid by the slot's price. The classic
// BOUNCING ROCKS, LOTS OF ROCKS and MEGA ROCKS refund one rock more than they
// throw, and every volley keeps its refund when fire() swings at a foe in
// front instead of throwing. With the setting on a volley refunds exactly
// the rocks it throws, and a blocked one costs the one swing fire() made.
#include <openglad/core/constants.h>
#include <openglad/gameplay/families/family_descriptor.h>
#include <openglad/gameplay/families/family_registry.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
#include "test_family_hook_dispatch.h"
#include <initializer_list>
#include <string>

namespace {

GameWorld& world()
{
    return og::runtime::current_session->myscreen_->world();
}

void ensure_level_loaded()
{
    if (world().grid.valid() && world().pixmaxx > 0 && world().pixmaxy > 0)
        return;
    world().id = 1;
    (void)og::runtime::current_session->myscreen_->load_level();
}

// A level-6 elf at (100, 100) facing right, open ground ahead.
walker* make_elf(float mp)
{
    ensure_level_loaded();
    guy g(FAMILY_ELF);
    g.teamnum = 1;
    g.upgrade_to_level(6, true);
    auto w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_);
    if (w)
    {
        w->setxy(100, 100);
        w->set_curdir(static_cast<signed char>(FACE_RIGHT));
        w->set_lastx(w->stepsize());
        w->set_lasty(0);
        w->set_busy(0);
        w->stats()->set_max_magicpoints(2000);
        w->stats()->set_magicpoints(mp);
    }
    return w.release();
}

int count_rocks()
{
    int n = 0;
    for (auto& uptr : world().weaplist)
        if (uptr && !uptr->dead() && uptr->family() == FAMILY_ROCK)
            ++n;
    return n;
}

class GameplayFixesElf : public ::testing::Test
{
protected:
    void SetUp() override
    {
        ensure_level_loaded();
        fixes_before_ = world().gameplay_fixes;
        world().delete_objects();
    }
    void TearDown() override
    {
        world().gameplay_fixes = fixes_before_;
        world().delete_objects();
    }

private:
    short fixes_before_ = 0;
};

struct Slot
{
    int slot;
    float price;
    int rocks;
    int classic_refund;
};

constexpr Slot kSlots[] = {
    {1, 10.0f, 2, 2},  // ROCKS: already exact
    {2, 20.0f, 2, 3},  // BOUNCING ROCKS
    {3, 30.0f, 3, 4},  // LOTS OF ROCKS
    {4, 40.0f, 4, 5},  // MEGA ROCKS
};

} // namespace

// Open ground: every rock flies. The classic volleys of slots 2-4 end one
// weapon cost above the slot's price; with the setting on every slot costs
// exactly its price.
TEST_F(GameplayFixesElf, volleys_refund_exactly_the_rocks_fired)
{
    for (const Slot& s : kSlots)
    {
        for (const short fixes : {short{0}, short{1}})
        {
            world().delete_objects();
            walker* w = make_elf(600.0f);
            ASSERT_NE(nullptr, w);
            const float wc = static_cast<float>(w->stats()->weapon_cost());
            ASSERT_GT(wc, 0.0f);
            w->set_current_special(static_cast<char>(s.slot));
            world().gameplay_fixes = fixes;
            ASSERT_TRUE(w->special()) << "slot " << s.slot << " fixes=" << fixes;
            EXPECT_EQ(s.rocks, count_rocks()) << "slot " << s.slot << " fixes=" << fixes;
            const float expected = (fixes == 0)
                ? 600.0f - s.price + static_cast<float>(s.classic_refund - s.rocks) * wc
                : 600.0f - s.price;
            EXPECT_FLOAT_EQ(expected, w->stats()->magicpoints())
                << "slot " << s.slot << " fixes=" << fixes;
            delete w;
        }
    }
}

// A foe right in front: fire() swings at it (charging one weapon cost) and
// returns nothing, so the volley is refused on its first rock. The classic
// cast keeps the whole refund: the swing is free and the elf ends richer
// by (refund - 1) weapon costs. With the setting on the rocks that never
// flew are taken back and the refused cast costs the one swing.
TEST_F(GameplayFixesElf, a_blocked_volley_costs_one_swing)
{
    for (const Slot& s : kSlots)
    {
        for (const short fixes : {short{0}, short{1}})
        {
            world().delete_objects();
            walker* w = make_elf(600.0f);
            ASSERT_NE(nullptr, w);
            const float wc = static_cast<float>(w->stats()->weapon_cost());
            walker* foe = world().add_ob(Order::Living, FAMILY_TOWER1);
            ASSERT_NE(nullptr, foe);
            foe->set_team_num(2);
            foe->setxy(static_cast<Sint32>(w->xpos() + w->sizex() + 1), static_cast<Sint32>(w->ypos()));
            w->set_current_special(static_cast<char>(s.slot));
            world().gameplay_fixes = fixes;
            walker::SpecialFailure why = walker::SpecialFailure::None;
            std::string reason;
            EXPECT_FALSE(w->special(&why, &reason)) << "slot " << s.slot << " fixes=" << fixes;
            EXPECT_EQ(std::string("COULD NOT FIRE"), reason);
            EXPECT_EQ(0, count_rocks()) << "nothing flew";
            const float expected = (fixes == 0)
                ? 600.0f + static_cast<float>(s.classic_refund - 1) * wc
                : 600.0f - wc;
            EXPECT_FLOAT_EQ(expected, w->stats()->magicpoints())
                << "slot " << s.slot << " fixes=" << fixes;
            delete w;
        }
    }
}

// ROCKS whose second rock cannot be paid for: the cast is called directly
// (the slot's price would refuse it first in play) with a pool of minus one
// weapon cost, so the refund pays the first rock and the second fails
// fire()'s own mana check without charging. Classic: the refund for the
// second rock is kept. With the setting on it is taken back.
TEST_F(GameplayFixesElf, rocks_take_back_the_second_rock_that_never_flew)
{
    const auto* fd = get_family_descriptor(FAMILY_ELF);
    ASSERT_NE(nullptr, fd);
    for (const short fixes : {short{0}, short{1}})
    {
        world().delete_objects();
        walker* w = make_elf(0.0f);
        ASSERT_NE(nullptr, w);
        const float wc = static_cast<float>(w->stats()->weapon_cost());
        w->stats()->set_magicpoints(-wc);
        w->set_current_special(1);
        world().gameplay_fixes = fixes;
        EXPECT_FALSE(og::test::do_special(*fd, w)) << "fixes=" << fixes;
        EXPECT_EQ(1, count_rocks()) << "one rock flew, fixes=" << fixes;
        const float expected = (fixes == 0) ? 0.0f : -wc;
        EXPECT_FLOAT_EQ(expected, w->stats()->magicpoints()) << "fixes=" << fixes;
        delete w;
    }
}
