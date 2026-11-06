// The druid's GROW TREE and SUMMON FAERIE with the Gameplay Fixes setting
// (packs/core/families/living-13-druid.lua). Both casts refund one weapon
// cost and then fire() the seed bolt. With a foe right in front, fire()
// swings at it instead (charging the weapon cost) and returns nothing, so
// the cast is refused with no tree and no faerie. The classic cast keeps
// the refund: the swing is free. With the setting on the refund is taken
// back and the refused cast costs the one swing.
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/game_context.h>
#include <openglad/interface/guy_create.h>
#include <openglad/interface/screen.h>
#include <openglad/legacy/base.h>
#include <gtest/gtest.h>
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

// A level-5 druid at (x, y) facing `face` (lastx / lasty one step along it),
// with `mp` mana and no busy.
walker* make_druid(short x, short y, int face, float mp)
{
    guy g(FAMILY_DRUID);
    g.teamnum = 1;
    g.upgrade_to_level(5, true);
    walker* w = guy_create_walker_owned(g, og::runtime::current_session->myscreen_).release();
    if (w == nullptr)
        return nullptr;
    w->setxy(x, y);
    w->set_curdir(static_cast<signed char>(face));
    w->set_lastx(face == FACE_RIGHT ? w->stepsize() : 0.0f);
    w->set_lasty(face == FACE_UP ? -w->stepsize() : 0.0f);
    w->set_busy(0);
    w->stats()->set_max_magicpoints(500);
    w->stats()->set_magicpoints(mp);
    return w;
}

// The grid cell under the leading edge of the pad of a caster facing RIGHT,
// at its middle row.
std::size_t pad_cell(const walker& w)
{
    return static_cast<std::size_t>((w.xpos() + w.sizex() + 1) / GRID_SIZE +
                                    world().grid.w * ((w.ypos() + w.sizey() / 2) / GRID_SIZE));
}

// One cast of `slot` with terrain on the pad: `tile` under the pad of a
// druid facing RIGHT at (100, 100), or, with tile < 0, the druid two pixels
// under the map's top edge facing UP. The MP the druid ends with; the cast
// must refuse COULD NOT FIRE and make nothing, and with the setting on it
// must draw nothing and make no entity.
float terrain_cast(int slot, short fixes, float mp, int tile)
{
    world().delete_objects();
    ensure_level_loaded();
    walker* w = tile >= 0 ? make_druid(100, 100, FACE_RIGHT, mp) : make_druid(100, 2, FACE_UP, mp);
    EXPECT_NE(nullptr, w);
    if (w == nullptr)
        return 0.0f;
    const std::size_t idx = pad_cell(*w);
    const std::uint8_t old = world().grid.data[idx];
    if (tile >= 0)
        world().grid.data[idx] = static_cast<std::uint8_t>(tile);
    EXPECT_EQ(walker::PadAnswer::Terrain, w->fire_pad_answer())
        << "precondition: terrain on the lightning's pad, slot " << slot << " tile " << tile;
    const int faeries_before = count_living(static_cast<char>(FAMILY_FAERIE));
    const std::uint32_t rng_before = world().rng_.state_;
    const std::size_t entities_before =
        world().oblist.size() + world().fxlist.size() + world().weaplist.size();

    w->set_current_special(static_cast<char>(slot));
    world().gameplay_fixes = fixes;
    walker::SpecialFailure why = walker::SpecialFailure::None;
    std::string reason;
    EXPECT_FALSE(w->special(&why, &reason)) << "slot " << slot << " fixes=" << fixes;
    EXPECT_EQ(std::string("COULD NOT FIRE"), reason) << "slot " << slot << " fixes=" << fixes;
    EXPECT_EQ(0, count_trees()) << "no tree, slot " << slot << " fixes=" << fixes;
    EXPECT_EQ(faeries_before, count_living(static_cast<char>(FAMILY_FAERIE)))
        << "no faerie, slot " << slot << " fixes=" << fixes;
    if (fixes != 0)
    {
        EXPECT_EQ(rng_before, world().rng_.state_) << "nothing drawn, slot " << slot;
        EXPECT_EQ(entities_before,
                  world().oblist.size() + world().fxlist.size() + world().weaplist.size())
            << "nothing made, slot " << slot;
    }
    const float after = w->stats()->magicpoints();
    world().grid.data[idx] = old;
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

// Terrain on the lightning's pad (a wall face, a tree crown, the map edge):
// fire() would swing at nothing. The classic cast keeps its refund and pays
// the swing: it ends where it began, the waver drawn. At the base the
// setting-on cast took the refund back as well and ended a weapon cost
// poorer for a swing at nothing (#410). With the setting on the cast now
// asks the pad first (gf.pad_blocked_by_terrain, fix AC) and refuses COULD
// NOT FIRE before the refund and the swing: the pool untouched, nothing
// drawn, nothing made. A tree crown is terrain for the lightning (it does
// not walk the forest), unlike the elf's rock.
//
// RED at the base: every setting-on arm reads mp - 4.
TEST_F(GameplayFixesDruid, a_cast_at_terrain_spends_nothing_with_the_setting_on)
{
    struct Case
    {
        int slot;
        float mp;
        int tile;
        const char* what;
    };
    for (const Case c : {Case{1, 23.0f, PIX_H_WALL1, "GROW TREE, a wall"},
                         Case{1, 23.0f, PIX_TREE_T1, "GROW TREE, a tree crown"},
                         Case{2, 100.0f, PIX_H_WALL1, "SUMMON FAERIE, a wall"},
                         Case{2, 100.0f, PIX_TREE_T1, "SUMMON FAERIE, a tree crown"}})
    {
        EXPECT_FLOAT_EQ(c.mp, terrain_cast(c.slot, 0, c.mp, c.tile)) << c.what << ", classic";
        EXPECT_FLOAT_EQ(c.mp, terrain_cast(c.slot, 1, c.mp, c.tile)) << c.what << ", the setting on";
    }
}

// The map edge: the druid two pixels under the map's top edge facing UP has
// its pad above the map. The same two answers.
//
// RED at the base: the setting-on arms read mp - 4.
TEST_F(GameplayFixesDruid, a_cast_at_the_map_edge_spends_nothing)
{
    EXPECT_FLOAT_EQ(23.0f, terrain_cast(1, 0, 23.0f, -1)) << "GROW TREE, classic";
    EXPECT_FLOAT_EQ(23.0f, terrain_cast(1, 1, 23.0f, -1)) << "GROW TREE, the setting on";
    EXPECT_FLOAT_EQ(100.0f, terrain_cast(2, 0, 100.0f, -1)) << "SUMMON FAERIE, classic";
    EXPECT_FLOAT_EQ(100.0f, terrain_cast(2, 1, 100.0f, -1)) << "SUMMON FAERIE, the setting on";
}
