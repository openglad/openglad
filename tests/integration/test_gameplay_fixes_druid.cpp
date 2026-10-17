// The druid's GROW TREE and SUMMON FAERIE with the Gameplay Fixes setting
// (packs/core/families/living-13-druid.lua). Both casts refund one weapon
// cost and then fire() the seed bolt. With a foe right in front, fire()
// swings at it instead (charging the weapon cost) and returns nothing, so
// the cast is refused with no tree and no faerie. The classic cast keeps
// the refund: the swing is free. With the setting on the refund is taken
// back and the refused cast costs the one swing.
#include <openglad/core/constants.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
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

int count_living(char family)
{
    int n = 0;
    for (auto& uptr : world().oblist)
        if (uptr && !uptr->dead() && uptr->query_order() == Order::Living && uptr->family() == family)
            ++n;
    return n;
}

int count_trees()
{
    int n = 0;
    for (auto& uptr : world().weaplist)
        if (uptr && !uptr->dead() && uptr->family() == FAMILY_TREE)
            ++n;
    return n;
}

class GameplayFixesDruid : public ::testing::Test
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

// One cast of `slot` by a level-5 druid facing a tower that touches the
// spot the seed bolt would start from. Returns the MP the druid ends with.
float blocked_cast(int slot, short fixes, float mp, float* weapon_cost)
{
    world().delete_objects();
    ensure_level_loaded();
    guy g(FAMILY_DRUID);
    g.teamnum = 1;
    g.upgrade_to_level(5, true);
    walker* w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_).release();
    EXPECT_NE(nullptr, w);
    if (w == nullptr)
        return 0.0f;
    w->setxy(100, 100);
    w->set_curdir(static_cast<signed char>(FACE_RIGHT));
    w->set_lastx(w->stepsize());
    w->set_lasty(0);
    w->set_busy(0);
    w->stats()->set_max_magicpoints(500);
    w->stats()->set_magicpoints(mp);
    *weapon_cost = static_cast<float>(w->stats()->weapon_cost());

    walker* foe = world().add_ob(Order::Living, FAMILY_TOWER1);
    EXPECT_NE(nullptr, foe);
    if (foe != nullptr)
    {
        foe->set_team_num(2);
        foe->setxy(static_cast<Sint32>(w->xpos() + w->sizex() + 1), static_cast<Sint32>(w->ypos()));
    }
    const int faeries_before = count_living(static_cast<char>(FAMILY_FAERIE));

    w->set_current_special(static_cast<char>(slot));
    world().gameplay_fixes = fixes;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
    EXPECT_FALSE(w->special(&why, &reason)) << "slot " << slot << " fixes=" << fixes;
    EXPECT_EQ(std::string("COULD NOT FIRE"), reason) << "slot " << slot << " fixes=" << fixes;
    EXPECT_EQ(0, count_trees()) << "no tree, slot " << slot << " fixes=" << fixes;
    EXPECT_EQ(faeries_before, count_living(static_cast<char>(FAMILY_FAERIE)))
        << "no faerie, slot " << slot << " fixes=" << fixes;
    const float after = w->stats()->magicpoints();
    delete w;
    return after;
}

} // namespace

// GROW TREE (15 MP) from a 23 MP pool, weapon cost 4: the classic refused
// cast ends at 23 (refund 4, swing 4); with the setting on at 19.
TEST_F(GameplayFixesDruid, a_blocked_tree_costs_the_swing)
{
    float wc = 0.0f;
    const float off = blocked_cast(1, 0, 23.0f, &wc);
    EXPECT_FLOAT_EQ(4.0f, wc) << "the druid's fire_mp_cost";
    EXPECT_FLOAT_EQ(23.0f, off) << "the classic swing is free";
    const float on = blocked_cast(1, 1, 23.0f, &wc);
    EXPECT_FLOAT_EQ(19.0f, on) << "with the setting on the swing costs its 4 MP";
}

// SUMMON FAERIE (80 MP) the same way from a 100 MP pool.
TEST_F(GameplayFixesDruid, a_blocked_faerie_costs_the_swing)
{
    float wc = 0.0f;
    const float off = blocked_cast(2, 0, 100.0f, &wc);
    EXPECT_FLOAT_EQ(100.0f, off) << "the classic swing is free";
    const float on = blocked_cast(2, 1, 100.0f, &wc);
    EXPECT_FLOAT_EQ(100.0f - wc, on) << "with the setting on the swing costs one weapon cost";
}

// Open ground: the setting changes nothing. One tree, and the cast costs
// its 15 MP on both values.
TEST_F(GameplayFixesDruid, an_open_tree_costs_its_price_on_both_values)
{
    for (const short fixes : {short{0}, short{1}})
    {
        world().delete_objects();
        guy g(FAMILY_DRUID);
        g.teamnum = 1;
        g.upgrade_to_level(5, true);
        walker* w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_).release();
        ASSERT_NE(nullptr, w);
        w->setxy(100, 100);
        w->set_curdir(static_cast<signed char>(FACE_RIGHT));
        w->set_lastx(w->stepsize());
        w->set_lasty(0);
        w->set_busy(0);
        w->stats()->set_max_magicpoints(500);
        w->stats()->set_magicpoints(100.0f);
        w->set_current_special(1);
        world().gameplay_fixes = fixes;
        EXPECT_TRUE(w->special()) << "fixes=" << fixes;
        EXPECT_EQ(1, count_trees()) << "fixes=" << fixes;
        EXPECT_FLOAT_EQ(85.0f, w->stats()->magicpoints()) << "fixes=" << fixes;
        delete w;
    }
}
