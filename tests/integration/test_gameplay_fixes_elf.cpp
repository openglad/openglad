// The elf's rock volleys with the Gameplay Fixes setting
// (packs/core/families/living-01-elf.lua). A volley pre-refunds weapon costs
// so that the rocks it throws are paid by the slot's price. The classic
// BOUNCING ROCKS, LOTS OF ROCKS and MEGA ROCKS refund one rock more than they
// throw, and every volley keeps its refund when fire() swings at a foe in
// front instead of throwing. With the setting on a volley refunds exactly
// the rocks it throws, and a blocked one costs the one swing fire() made.
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
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
#include <cstddef>
#include <cstdint>
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

// A tile (a wall by default) under the pad fire() would place the weapon
// of `w`, facing RIGHT, on, for the scope's lifetime: the cell under the
// pad's leading edge, at the caster's middle row. The level is shared by
// the group's tests, so the cell goes back as it was.
class TileUnderPad
{
public:
    explicit TileUnderPad(walker& w, int tile = PIX_H_WALL1)
        : idx_(static_cast<std::size_t>((w.xpos() + w.sizex() + 1) / GRID_SIZE +
                                        world().grid.w * ((w.ypos() + w.sizey() / 2) / GRID_SIZE))),
          old_(world().grid.data[idx_])
    {
        world().grid.data[idx_] = static_cast<std::uint8_t>(tile);
    }
    ~TileUnderPad() { world().grid.data[idx_] = old_; }
    TileUnderPad(const TileUnderPad&) = delete;
    TileUnderPad& operator=(const TileUnderPad&) = delete;

private:
    std::size_t idx_;
    std::uint8_t old_;
};

// What a refused volley could have spent or made.
struct Spent
{
    float mp = 0.0f;
    std::uint32_t rng = 0;
    std::size_t oblist = 0;
    std::size_t fxlist = 0;
    std::size_t weaplist = 0;
};

Spent spent(walker& w)
{
    return {w.stats()->magicpoints(), world().rng_.state_, world().oblist.size(),
            world().fxlist.size(), world().weaplist.size()};
}

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

// A wall under the pad: fire() would swing at nothing and still charge its
// one weapon cost. The classic volley keeps its whole refund and pays the
// swing: it ends (refund - 1) weapon costs richer. With the setting on the
// volley asks the pad first (gf.pad_blocked_by_terrain, fix AC) and refuses
// COULD NOT FIRE before the refund and the swing: nothing spent, nothing
// drawn (fire()'s waver), nothing made.
//
// RED at the base (the reporter's case, #410): the setting-on arm reads
// 600 - wc, the swing charged.
TEST_F(GameplayFixesElf, a_volley_at_a_wall_spends_nothing_with_the_setting_on)
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
            {
                TileUnderPad wall(*w);
                ASSERT_EQ(walker::PadAnswer::Terrain, w->fire_pad_answer())
                    << "precondition: the wall is under the rock's pad";
                w->set_current_special(static_cast<char>(s.slot));
                world().gameplay_fixes = fixes;
                const Spent before = spent(*w);
                walker::SpecialFailure why = walker::SpecialFailure::None;
                std::string reason;
                EXPECT_FALSE(w->special(&why, &reason)) << "slot " << s.slot << " fixes=" << fixes;
                EXPECT_EQ(std::string("COULD NOT FIRE"), reason) << "slot " << s.slot << " fixes=" << fixes;
                EXPECT_EQ(0, count_rocks()) << "nothing flew, slot " << s.slot << " fixes=" << fixes;
                const Spent after = spent(*w);
                if (fixes == 0)
                {
                    EXPECT_FLOAT_EQ(600.0f + static_cast<float>(s.classic_refund - 1) * wc, after.mp)
                        << "the classic refund kept and the swing paid, slot " << s.slot;
                }
                else
                {
                    EXPECT_FLOAT_EQ(600.0f, after.mp) << "nothing spent, slot " << s.slot;
                    EXPECT_EQ(before.rng, after.rng) << "nothing drawn, slot " << s.slot;
                    EXPECT_EQ(before.oblist, after.oblist) << "slot " << s.slot;
                    EXPECT_EQ(before.fxlist, after.fxlist) << "slot " << s.slot;
                    EXPECT_EQ(before.weaplist, after.weaplist) << "no rock made, slot " << s.slot;
                }
            }
            delete w;
        }
    }
}

// The map edge: the elf two pixels under the map's top edge, throwing UP,
// has its pad above the map; the grid refuses it as it refuses a wall.
// Classic: the refund kept, the swing paid. With the setting on: refused
// with nothing spent.
//
// RED at the base (#410): the setting-on arm reads 600 - wc.
TEST_F(GameplayFixesElf, a_volley_at_the_map_edge_spends_nothing)
{
    for (const Slot& s : kSlots)
    {
        for (const short fixes : {short{0}, short{1}})
        {
            world().delete_objects();
            walker* w = make_elf(600.0f);
            ASSERT_NE(nullptr, w);
            const float wc = static_cast<float>(w->stats()->weapon_cost());
            w->setxy(100, 2);
            w->set_curdir(static_cast<signed char>(FACE_UP));
            w->set_lastx(0);
            w->set_lasty(-w->stepsize());
            ASSERT_EQ(walker::PadAnswer::Terrain, w->fire_pad_answer())
                << "precondition: the pad is off the map";
            w->set_current_special(static_cast<char>(s.slot));
            world().gameplay_fixes = fixes;
            const Spent before = spent(*w);
            walker::SpecialFailure why = walker::SpecialFailure::None;
            std::string reason;
            EXPECT_FALSE(w->special(&why, &reason)) << "slot " << s.slot << " fixes=" << fixes;
            EXPECT_EQ(std::string("COULD NOT FIRE"), reason) << "slot " << s.slot << " fixes=" << fixes;
            EXPECT_EQ(0, count_rocks()) << "slot " << s.slot << " fixes=" << fixes;
            const Spent after = spent(*w);
            const float expected = (fixes == 0)
                ? 600.0f + static_cast<float>(s.classic_refund - 1) * wc
                : 600.0f;
            EXPECT_FLOAT_EQ(expected, after.mp) << "slot " << s.slot << " fixes=" << fixes;
            if (fixes != 0)
            {
                EXPECT_EQ(before.rng, after.rng) << "nothing drawn, slot " << s.slot;
                EXPECT_EQ(before.weaplist, after.weaplist) << "no rock made, slot " << s.slot;
            }
            delete w;
        }
    }
}

// A tree crown is not terrain for a rock: core:rock walks the forest
// (FORESTWALK), so the grid lets it through and the volley flies. The
// question asks the rock's own rules, so a crown under the pad does not
// refuse, on either value.
TEST_F(GameplayFixesElf, a_tree_crown_on_the_pad_lets_the_rocks_fly)
{
    for (const short fixes : {short{0}, short{1}})
    {
        world().delete_objects();
        walker* w = make_elf(600.0f);
        ASSERT_NE(nullptr, w);
        {
            TileUnderPad crown(*w, PIX_TREE_T1);
            ASSERT_EQ(walker::PadAnswer::Clear, w->fire_pad_answer())
                << "precondition: the rock walks the forest";
            w->set_current_special(1);
            world().gameplay_fixes = fixes;
            EXPECT_TRUE(w->special()) << "fixes=" << fixes;
            EXPECT_EQ(2, count_rocks()) << "fixes=" << fixes;
            EXPECT_FLOAT_EQ(590.0f, w->stats()->magicpoints()) << "ROCKS' price, fixes=" << fixes;
        }
        delete w;
    }
}
