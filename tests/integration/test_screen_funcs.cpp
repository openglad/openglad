#include <openglad/resources/gloader.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/core/constants.h>
#include <openglad/core/pixdefs.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

// myscreen is now a macro defined in base.h (via game_session.h)

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader;
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (!w) return nullptr;
    w->setxy(50, 50);
    return w;
}

namespace {

screen& scr()
{
    return *og::runtime::current_session->myscreen_;
}

GameWorld& world()
{
    return scr().world();
}

// Park a fresh living in the world's oblist (GameWorld::is_tracked_entity only
// accepts walkers that live in one of the world lists, and the obmap spiral
// prunes anything else) and hand back the borrowed pointer.
walker* spawn_in_world(char family, unsigned char team, short x, short y)
{
    auto w = create_living(family);
    if (!w) return nullptr;
    walker* raw = w.get();
    world().oblist.push_back(std::move(w));
    raw->set_team_num(team);
    raw->setxy(x, y);
    return raw;
}

void fill_grid(unsigned char tile)
{
    const std::size_t n =
        static_cast<std::size_t>(world().grid.w) * static_cast<std::size_t>(world().grid.h);
    for (std::size_t i = 0; i < n; ++i)
        world().grid.data[i] = tile;
}

// A decor plane matching the grid's dims -- the only shape GameWorld consults
// (damage_tile / query_grid_passable both gate on validity AND matching w/h).
// create_new_grid() frees the decor plane, so this must run after it.
void allocate_decor_plane(unsigned char fill)
{
    GameWorld& w = world();
    ASSERT_TRUE(w.grid.valid()) << "grid must exist before a decor plane";
    w.decor.free();
    w.decor.frames = 1;
    w.decor.w = w.grid.w;
    w.decor.h = w.grid.h;
    const std::size_t size =
        static_cast<std::size_t>(w.decor.w) * static_cast<std::size_t>(w.decor.h);
    w.decor.data = std::make_unique<unsigned char[]>(size);
    for (std::size_t i = 0; i < size; ++i)
        w.decor.data[i] = fill;
}

} // namespace

// ---------------------------------------------------------------------------
// first_of tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_first_of_found)
{
    // Start from an empty world: without this, first_of could answer with a
    // soldier a sibling test left in oblist and the push below would prove
    // nothing.
    world().delete_objects();
    ASSERT_TRUE(world().oblist.empty()) << "the world starts empty for this test";
    ASSERT_EQ(nullptr, scr().first_of(Order::Living, FAMILY_SOLDIER))
        << "and first_of finds no soldier before we push one";

    auto w = create_living(FAMILY_SOLDIER);
	    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

	    // Add to oblist
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w));

    walker* found = og::runtime::current_session->myscreen_->first_of(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(found != nullptr) << "first_of should find the soldier";
    ASSERT_EQ((int)FAMILY_SOLDIER, (int)found->family()) << "found should be soldier";

    // Remove from oblist (don't double-delete)
    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


TEST(ScreenFuncs, screen_first_of_not_found)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    walker* found = og::runtime::current_session->myscreen_->first_of(Order::Living, FAMILY_ARCHMAGE, 99);
    ASSERT_EQ(nullptr, found) << "first_of should return null when no matching object exists";
}


TEST(ScreenFuncs, screen_first_of_with_team)
{
    og::runtime::current_session->myscreen_->world().delete_objects();
    auto w = create_living(FAMILY_SOLDIER);
	    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
	    w->set_team_num(7);

	    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w));

    walker* found = og::runtime::current_session->myscreen_->first_of(Order::Living, FAMILY_SOLDIER, 7);
    ASSERT_TRUE(found != nullptr) << "first_of with matching team should find it";

    walker* not_found = og::runtime::current_session->myscreen_->first_of(Order::Living, FAMILY_SOLDIER, 99);
    ASSERT_EQ(nullptr, not_found) << "first_of with wrong team should not find a team-99 unit";

    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


// ---------------------------------------------------------------------------
// find_in_range tests
//
// GameWorld::find_in_range returns exactly the non-dead, non-dormant walkers of
// `somelist` whose distance_to_ob(ob) is <= range, and writes that count into
// *howmany.
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_find_in_range_basic)
{
    world().delete_objects();

    auto seeker = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, seeker.get()) << "create seeker should succeed";
    seeker->setxy(100, 100);

    walker* target = spawn_in_world(FAMILY_MAGE, 0, 110, 100);
    ASSERT_NE(nullptr, target) << "create target should succeed";

    std::int32_t howmany = -1;
    auto result = world().find_in_range(world().oblist, 500, &howmany, seeker.get());
    ASSERT_EQ(1, howmany)
        << "find_in_range must report exactly the one walker standing inside range";
    ASSERT_EQ(1u, result.size())
        << "and the returned list must hold exactly that one walker";
    ASSERT_EQ(target, result.front())
        << "the returned walker must be the target we placed 10px away";

    seeker.reset();
    world().delete_objects();
}


TEST(ScreenFuncs, screen_find_in_range_out_of_range)
{
    world().delete_objects();

    auto seeker = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, seeker.get()) << "create seeker should succeed";
    seeker->setxy(50, 50);

    walker* target = spawn_in_world(FAMILY_MAGE, 0, 250, 250);
    ASSERT_NE(nullptr, target) << "create target should succeed";

    // walker::distance_to_ob is Manhattan: |250-50| + |250-50|.
    ASSERT_EQ(400, seeker->distance_to_ob(target))
        << "the target must sit 400 units away for the range contrast below to mean anything";

    std::int32_t howmany = -1;
    auto excluded = world().find_in_range(world().oblist, 5, &howmany, seeker.get());
    ASSERT_EQ(0, howmany) << "a walker 400 units away is outside range 5";
    ASSERT_TRUE(excluded.empty()) << "and must not appear in the returned list";

    howmany = -1;
    auto included = world().find_in_range(world().oblist, 500, &howmany, seeker.get());
    ASSERT_EQ(1, howmany) << "the same walker IS inside range 500 (paired control)";
    ASSERT_EQ(1u, included.size()) << "exactly one walker inside range 500";
    ASSERT_EQ(target, included.front()) << "and it is the target we placed";

    seeker.reset();
    world().delete_objects();
}


// ---------------------------------------------------------------------------
// find_foes_in_range tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_find_foes_in_range)
{
    world().delete_objects();

    auto seeker = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, seeker.get()) << "create seeker should succeed";
    seeker->set_team_num(0);
    seeker->setxy(100, 100);

    walker* enemy = spawn_in_world(FAMILY_SMALL_SLIME, 1, 110, 100);
    walker* ally = spawn_in_world(FAMILY_ARCHER, 0, 112, 100);
    ASSERT_NE(nullptr, enemy) << "create enemy should succeed";
    ASSERT_NE(nullptr, ally) << "create ally should succeed";

    std::int32_t howmany = -1;
    auto result = world().find_foes_in_range(world().oblist, 500, &howmany, seeker.get());
    ASSERT_EQ(1, howmany)
        << "only the opposing-team walker counts as a foe; the ally is in range too";
    ASSERT_EQ(1u, result.size()) << "exactly one foe returned";
    ASSERT_EQ(enemy, result.front())
        << "find_foes_in_range must keep its is_friendly()==0 filter";

    seeker.reset();
    world().delete_objects();
}


// ---------------------------------------------------------------------------
// find_friends_in_range tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_find_friends_in_range)
{
    world().delete_objects();

    auto seeker = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, seeker.get()) << "create seeker should succeed";
    seeker->set_team_num(0);
    seeker->setxy(100, 100);

    walker* friend_w = spawn_in_world(FAMILY_ARCHER, 0, 110, 100);
    walker* enemy = spawn_in_world(FAMILY_SMALL_SLIME, 1, 112, 100);
    ASSERT_NE(nullptr, friend_w) << "create friend should succeed";
    ASSERT_NE(nullptr, enemy) << "create enemy should succeed";

    std::int32_t howmany = -1;
    auto result = world().find_friends_in_range(world().oblist, 500, &howmany, seeker.get());
    ASSERT_EQ(1, howmany)
        << "only the same-team walker counts as a friend; the enemy is in range too";
    ASSERT_EQ(1u, result.size()) << "exactly one friend returned";
    ASSERT_EQ(friend_w, result.front())
        << "find_friends_in_range must keep its is_friendly() filter the right way round";

    seeker.reset();
    world().delete_objects();
}


// ---------------------------------------------------------------------------
// damage_tile tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_damage_tile_out_of_bounds)
{
    world().create_new_grid();
    world().clear_grid_dirty_tiles();
    ASSERT_TRUE(world().grid.valid()) << "create_new_grid must hand us a valid grid";

    const int gw = world().grid.w;
    const int gh = world().grid.h;
    world().grid.data[0] = PIX_GRASS2;

    // NOTE: damage_tile divides by GRID_SIZE with C++ truncation, so a
    // coordinate inside the first cell's width (e.g. -10) lands on cell 0 and
    // is NOT rejected. The guard bites from one whole cell out; that is the
    // behaviour pinned here.
    ASSERT_EQ(0, (int)scr().damage_tile(-GRID_SIZE, 0))
        << "a negative x cell must be rejected";
    ASSERT_EQ(0, (int)scr().damage_tile(0, -GRID_SIZE))
        << "a negative y cell must be rejected";
    ASSERT_EQ(0, (int)scr().damage_tile(static_cast<short>(gw * GRID_SIZE), 0))
        << "x at grid.w must be rejected";
    ASSERT_EQ(0, (int)scr().damage_tile(0, static_cast<short>(gh * GRID_SIZE)))
        << "y at grid.h must be rejected";

    ASSERT_EQ((int)PIX_GRASS2, (int)world().grid.data[0])
        << "an out-of-bounds hit must never write the grid";
    ASSERT_TRUE(world().grid_dirty_tiles().empty())
        << "an out-of-bounds hit must never fold a dirty tile";
}


TEST(ScreenFuncs, screen_damage_tile_smoke)
{
    world().create_new_grid();
    world().clear_grid_dirty_tiles();
    ASSERT_TRUE(world().grid.valid()) << "create_new_grid must hand us a valid grid";

    const int gx = 100 / GRID_SIZE;
    const int gy = 100 / GRID_SIZE;
    const std::size_t loc = static_cast<std::size_t>(gy * world().grid.w + gx);
    world().grid.data[loc] = PIX_GRASS2;

    ASSERT_EQ((int)PIX_GRASS1_DAMAGED, (int)(unsigned char)scr().damage_tile(100, 100))
        << "damage_tile must char a grass cell and return the new byte";
    ASSERT_EQ((int)PIX_GRASS1_DAMAGED, (int)world().grid.data[loc])
        << "and must write that byte back into the grid";
    ASSERT_EQ(1u, world().grid_dirty_tiles().size())
        << "the charred cell must be folded into the dirty-tile list exactly once";
    ASSERT_EQ(gx, (int)world().grid_dirty_tiles().front().first)
        << "the dirty entry must carry the tile's x cell";
    ASSERT_EQ(gy, (int)world().grid_dirty_tiles().front().second)
        << "the dirty entry must carry the tile's y cell";

    // Already charred: the same byte comes back and no second dirty entry appears.
    ASSERT_EQ((int)PIX_GRASS1_DAMAGED, (int)(unsigned char)scr().damage_tile(100, 100))
        << "a second hit on charred grass returns the charred byte";
    ASSERT_EQ(1u, world().grid_dirty_tiles().size())
        << "and must not duplicate the dirty-tile entry";

    // A tile outside the grass family is returned untouched and never folded.
    world().grid.data[loc] = PIX_TREE_M1;
    ASSERT_EQ((int)PIX_TREE_M1, (int)(unsigned char)scr().damage_tile(100, 100))
        << "a non-grass tile is returned unchanged";
    ASSERT_EQ((int)PIX_TREE_M1, (int)world().grid.data[loc])
        << "a non-grass tile must not be rewritten";
    ASSERT_EQ(1u, world().grid_dirty_tiles().size())
        << "a non-grass tile must not be folded into the dirty-tile list";

    // Decor shields the ground: a decorated cell is exempt from the
    // grass->charred transform and returns 0, which also skips the caller's
    // dirty-tile fold. (create_new_grid frees the decor plane, so it has to be
    // allocated after the reset below.)
    world().create_new_grid();
    world().clear_grid_dirty_tiles();
    allocate_decor_plane(DECOR_NONE);
    ASSERT_TRUE(world().decor.valid()) << "the decor plane is allocated";
    world().grid.data[loc] = PIX_GRASS2;
    world().decor.data[loc] = DECOR_BOULDER_1;

    ASSERT_EQ(0, (int)scr().damage_tile(100, 100))
        << "a decorated cell is exempt from the transform and returns 0";
    ASSERT_EQ((int)PIX_GRASS2, (int)world().grid.data[loc])
        << "the exempt cell's base byte must survive the hit unchanged";
    ASSERT_TRUE(world().grid_dirty_tiles().empty())
        << "an exempt cell must never be folded into the dirty-tile list";

    // The exemption is the decor BYTE, not the cell: clear it and the very
    // same hit chars the very same grass.
    world().decor.data[loc] = DECOR_NONE;
    ASSERT_EQ((int)PIX_GRASS1_DAMAGED, (int)(unsigned char)scr().damage_tile(100, 100))
        << "with DECOR_NONE under it the same cell chars normally";
    ASSERT_EQ(1u, world().grid_dirty_tiles().size())
        << "and only then is the cell folded into the dirty-tile list";

    world().decor.free();
    world().clear_grid_dirty_tiles();
    world().create_new_grid();
}


// ---------------------------------------------------------------------------
// query_grid_passable tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_query_grid_passable_center)
{
    world().create_new_grid();

    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    w->stats()->set_bit_flags(BIT_ETHEREAL, 0);
    w->setxy(100, 100);

    ASSERT_TRUE(world().query_grid_passable(100, 100, w.get()))
        << "every cell of a fresh all-grass grid must be walkable";

    fill_grid(PIX_H_WALL1);
    ASSERT_FALSE(world().query_grid_passable(100, 100, w.get()))
        << "a wall under the footprint must block a non-ethereal walker";

    world().create_new_grid();
}


TEST(ScreenFuncs, screen_query_grid_passable_flying)
{
    world().create_new_grid();

    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";
    w->setxy(100, 100);
    w->set_flight_left(0);
    w->stats()->set_bit_flags(BIT_FLYING, 0);
    w->stats()->set_bit_flags(BIT_FORESTWALK, 0);
    w->stats()->set_bit_flags(BIT_ETHEREAL, 0);

    fill_grid(PIX_TREE_M1);
    ASSERT_FALSE(world().query_grid_passable(100, 100, w.get()))
        << "tree cells block a grounded walker";
    w->stats()->set_bit_flags(BIT_FLYING, 1);
    ASSERT_TRUE(world().query_grid_passable(100, 100, w.get()))
        << "BIT_FLYING is what lets a walker cross trees";

    fill_grid(PIX_H_WALL1);
    ASSERT_FALSE(world().query_grid_passable(100, 100, w.get()))
        << "walls stop flyers too - flight is not a wall exemption";
    w->stats()->set_bit_flags(BIT_ETHEREAL, 1);
    ASSERT_TRUE(world().query_grid_passable(100, 100, w.get()))
        << "only BIT_ETHEREAL passes through walls";

    world().create_new_grid();
}


TEST(ScreenFuncs, screen_query_grid_passable_out_of_bounds)
{
    world().create_new_grid();

    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    w->stats()->set_bit_flags(BIT_ETHEREAL, 0);
    w->setxy(100, 100);

    // Positive control on the same grid and the same walker: without it a
    // query_grid_passable that answered false for EVERYTHING would pass this
    // test.
    ASSERT_TRUE(world().query_grid_passable(100, 100, w.get()))
        << "an in-bounds cell of a fresh all-grass grid must be passable";

    ASSERT_FALSE(world().query_grid_passable(-10, -10, w.get()))
        << "a negative origin is rejected by the bounds guard";
    // The guard bites on the FOOTPRINT, not the origin: x itself is still on
    // the map here, x + sizex is not.
    ASSERT_TRUE(world().pixmaxx > 0) << "the world reports its pixel width";
    ASSERT_FALSE(world().query_grid_passable(
                     static_cast<float>(world().pixmaxx - 1), 100, w.get()))
        << "a footprint hanging past pixmaxx is rejected";
    ASSERT_FALSE(world().query_grid_passable(
                     100, static_cast<float>(world().pixmaxy - 1), w.get()))
        << "a footprint hanging past pixmaxy is rejected";

    // The bounds guard runs BEFORE the ethereal exemption, so not even a ghost
    // may stand off the map.
    w->stats()->set_bit_flags(BIT_ETHEREAL, 1);
    ASSERT_FALSE(world().query_grid_passable(-10, -10, w.get()))
        << "BIT_ETHEREAL is a wall exemption, never a map-edge exemption";
    w->stats()->set_bit_flags(BIT_ETHEREAL, 0);

    ASSERT_FALSE(world().query_grid_passable(100, 100, nullptr))
        << "a null walker has no footprint to test";

    world().create_new_grid();
}


// ---------------------------------------------------------------------------
// find_nearest_foe tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_find_nearest_foe_smoke)
{
    world().delete_objects();

    auto seeker = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, seeker.get()) << "create_walker should succeed";
    seeker->set_team_num(0);
    seeker->setxy(100, 100);

    ASSERT_EQ(nullptr, world().find_nearest_foe(seeker.get()))
        << "with no hostile in the world find_nearest_foe must return nullptr";

    walker* near_foe = spawn_in_world(FAMILY_SOLDIER, 1, 140, 100);   // distance 40
    walker* far_foe = spawn_in_world(FAMILY_SOLDIER, 1, 400, 100);    // distance 300
    ASSERT_NE(nullptr, near_foe) << "create near foe should succeed";
    ASSERT_NE(nullptr, far_foe) << "create far foe should succeed";
    ASSERT_EQ(40, seeker->distance_to_ob(near_foe)) << "near foe is 40 away";
    ASSERT_EQ(300, seeker->distance_to_ob(far_foe)) << "far foe is 300 away";

    ASSERT_EQ(near_foe, world().find_nearest_foe(seeker.get()))
        << "find_nearest_foe returns the closest hostile";

    near_foe->set_team_num(0); // now an ally
    ASSERT_EQ(far_foe, world().find_nearest_foe(seeker.get()))
        << "an ally is never a foe, however close - the is_friendly filter";

    seeker.reset();
    world().delete_objects();
}


// ---------------------------------------------------------------------------
// find_near_foe tests
// ---------------------------------------------------------------------------

// find_near_foe walks the obmap in an outward spiral and returns the first
// hostile it MEETS. That is a different rule from find_nearest_foe, which scans the
// whole oblist and returns the NEAREST hostile -- so the two disagree, and the
// disagreement is what this test pins.
TEST(ScreenFuncs, screen_find_near_foe_returns_the_spirals_first_hostile_not_the_nearest)
{
    world().delete_objects();

    auto seeker = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, seeker.get()) << "create_walker should succeed";
    seeker->set_team_num(0);
    seeker->setxy(100, 100);

    ASSERT_EQ(nullptr, world().find_near_foe(seeker.get()))
        << "with nothing in the world the spiral must fall through to nullptr";

    // 140,100 sits in the first obmap cell the spiral probes (one obmapres
    // (32px) step east of the seeker's own cell), so acquisition here is
    // deterministic.
    walker* other = spawn_in_world(FAMILY_SOLDIER, 0, 140, 100);
    ASSERT_NE(nullptr, other) << "create other should succeed";

    ASSERT_EQ(nullptr, world().find_near_foe(seeker.get()))
        << "a same-team walker in the spiral must never be acquired as a foe";

    other->set_team_num(1);
    ASSERT_EQ(other, world().find_near_foe(seeker.get()))
        << "the same walker on the opposing team IS the foe the spiral returns";

    // The discriminator. `decoy` stands 24px from the seeker, `other` 40px --
    // but the decoy's obmap cell is north-west of the seeker and the spiral
    // starts due east, so the spiral meets `other` first. A find_near_foe that
    // had quietly become `return find_nearest_foe(ob);` would answer `decoy` here.
    walker* decoy = spawn_in_world(FAMILY_SOLDIER, 1, 88, 88);
    ASSERT_NE(nullptr, decoy) << "create decoy should succeed";
    ASSERT_EQ(24, seeker->distance_to_ob(decoy)) << "the decoy really is the nearer foe";
    ASSERT_EQ(40, seeker->distance_to_ob(other)) << "and the spiral's first hit is farther";

    ASSERT_EQ(decoy, world().find_nearest_foe(seeker.get()))
        << "find_nearest_foe, the distance oracle, picks the nearer foe";
    ASSERT_EQ(other, world().find_near_foe(seeker.get()))
        << "find_near_foe picks by spiral order, not by distance";

    seeker.reset();
    world().delete_objects();
}


// ---------------------------------------------------------------------------
// do_notify test
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_do_notify_smoke)
{
    ASSERT_LE(1, (int)scr().numviews) << "the test session must own at least one view";
    viewscreen* v = scr().viewob[0].get();
    ASSERT_NE(nullptr, v) << "view 0 must exist";

    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, w.get()) << "create_walker should succeed";

    walker* saved_control = v->control;

    // 1) The view that controls `who` receives the line - exactly once. A
    //    broken `sent` flag would ALSO run the broadcast loop and fill slot 1.
    v->clear_text();
    v->control = w.get();
    scr().do_notify("Targeted", w.get());
    ASSERT_EQ(std::string("Targeted"), v->textlist[0])
        << "do_notify must write the message into the view that controls `who`";
    ASSERT_TRUE(v->textlist[1].empty())
        << "the owning-view write must suppress the broadcast fallback";

    // 2) Nobody controls `who` => broadcast to every view, also exactly once.
    v->clear_text();
    v->control = nullptr;
    scr().do_notify("Unowned", w.get());
    ASSERT_EQ(std::string("Unowned"), v->textlist[0])
        << "a message for an uncontrolled walker must still reach the view";
    ASSERT_TRUE(v->textlist[1].empty())
        << "the broadcast fallback must write each view exactly once";

    // 3) who == nullptr is always a broadcast.
    v->clear_text();
    scr().do_notify("Broadcast", nullptr);
    ASSERT_EQ(std::string("Broadcast"), v->textlist[0])
        << "do_notify(msg, nullptr) must broadcast to every view";
    ASSERT_TRUE(v->textlist[1].empty())
        << "a broadcast must not double-write a view";

    v->control = saved_control;
    v->clear_text();
}


// ---------------------------------------------------------------------------
// query_passable
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_query_passable_smoke)
{
    world().delete_objects();
    world().create_new_grid();

    auto seeker = create_living(FAMILY_SOLDIER);
    ASSERT_NE(nullptr, seeker.get()) << "create_walker should succeed";
    seeker->set_team_num(0);
    seeker->setxy(300, 300); // stand well clear of the probed spot

    ASSERT_TRUE(world().query_grid_passable(100, 100, seeker.get()))
        << "the probed spot is plain grass";
    ASSERT_TRUE(world().query_passable(100, 100, seeker.get()))
        << "an empty grass spot is passable";

    walker* blocker = spawn_in_world(FAMILY_SOLDIER, 0, 100, 100);
    ASSERT_NE(nullptr, blocker) << "create blocker should succeed";

    ASSERT_TRUE(world().query_grid_passable(100, 100, seeker.get()))
        << "the terrain under the blocker is still grass - the grid test is unchanged";
    ASSERT_FALSE(world().query_passable(100, 100, seeker.get()))
        << "query_passable must ALSO consult obmap occupancy, not just the grid";

    seeker.reset();
    world().delete_objects();
    world().create_new_grid();
}
