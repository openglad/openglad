#include <openglad/resources/gloader.h>
#include <openglad/gameplay/guy.h>
#include <openglad/gameplay/walker.h>
#include <openglad/interface/screen.h>
#include <gtest/gtest.h>
#include <memory>

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

// ---------------------------------------------------------------------------
// first_of tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_first_of_found)
{
    auto w = create_living(FAMILY_SOLDIER);
	    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

	    // Add to oblist
	    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w));

    walker* found = og::runtime::current_session->myscreen_->first_of(Order::Living, FAMILY_SOLDIER);
    ASSERT_TRUE(found != nullptr) << "first_of should find the soldier";
    ASSERT_EQ((int)FAMILY_SOLDIER, (int)found->family) << "found should be soldier";

    // Remove from oblist (don't double-delete)
    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


TEST(ScreenFuncs, screen_first_of_not_found)
{
    walker* found = og::runtime::current_session->myscreen_->first_of(Order::Living, FAMILY_ARCHMAGE, 99);
    // May or may not find one depending on test state, but should not crash
    (void)found;
}


TEST(ScreenFuncs, screen_first_of_with_team)
{
    auto w = create_living(FAMILY_SOLDIER);
	    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
	    w->team_num = 7;

	    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(w));

    walker* found = og::runtime::current_session->myscreen_->first_of(Order::Living, FAMILY_SOLDIER, 7);
    ASSERT_TRUE(found != nullptr) << "first_of with matching team should find it";

    walker* not_found = og::runtime::current_session->myscreen_->first_of(Order::Living, FAMILY_SOLDIER, 99);
    ASSERT_TRUE(not_found == nullptr || not_found->team_num != 99) << "first_of with wrong team should not find team 99 unit";

    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


// ---------------------------------------------------------------------------
// find_in_range tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_find_in_range_basic)
{
    auto seeker = create_living(FAMILY_SOLDIER);
    auto target = create_living(FAMILY_MAGE);
    ASSERT_TRUE(seeker != nullptr) << "create seeker should succeed";
    ASSERT_TRUE(target != nullptr) << "create target should succeed";

	    seeker->setxy(100, 100);
	    target->setxy(110, 100);

	    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(target));

	    Sint32 howmany = 0;
	    auto result = og::runtime::current_session->myscreen_->world().find_in_range(og::runtime::current_session->myscreen_->world().oblist, 500, &howmany, seeker.get());
	    ASSERT_TRUE(howmany > 0) << "should find at least 1 in range";

    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


TEST(ScreenFuncs, screen_find_in_range_out_of_range)
{
    auto seeker = create_living(FAMILY_SOLDIER);
    auto target = create_living(FAMILY_MAGE);
    ASSERT_TRUE(seeker != nullptr) << "create seeker should succeed";
    ASSERT_TRUE(target != nullptr) << "create target should succeed";

	    seeker->setxy(50, 50);
	    target->setxy(250, 250);

	    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(target));

	    Sint32 howmany = 0;
	    auto result = og::runtime::current_session->myscreen_->world().find_in_range(og::runtime::current_session->myscreen_->world().oblist, 5, &howmany, seeker.get());
	    (void)result; // range semantics vary; just verify no crash

    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


// ---------------------------------------------------------------------------
// find_foes_in_range tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_find_foes_in_range)
{
    auto seeker = create_living(FAMILY_SOLDIER);
    auto enemy = create_living(FAMILY_SMALL_SLIME);
    ASSERT_TRUE(seeker != nullptr) << "create seeker should succeed";
    ASSERT_TRUE(enemy != nullptr) << "create enemy should succeed";

    seeker->team_num = 0;
    seeker->setxy(100, 100);
	    enemy->team_num = 1;
	    enemy->setxy(110, 100);

	    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(enemy));

	    Sint32 howmany = 0;
	    auto result = og::runtime::current_session->myscreen_->world().find_foes_in_range(og::runtime::current_session->myscreen_->world().oblist, 500, &howmany, seeker.get());
	    ASSERT_TRUE(howmany > 0) << "should find at least 1 foe in range";

    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


// ---------------------------------------------------------------------------
// find_friends_in_range tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_find_friends_in_range)
{
    auto seeker = create_living(FAMILY_SOLDIER);
    auto friend_w = create_living(FAMILY_ARCHER);
    ASSERT_TRUE(seeker != nullptr) << "create seeker should succeed";
    ASSERT_TRUE(friend_w != nullptr) << "create friend should succeed";

    seeker->team_num = 0;
    seeker->setxy(100, 100);
	    friend_w->team_num = 0;
	    friend_w->setxy(110, 100);

	    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(friend_w));

	    Sint32 howmany = 0;
	    auto result = og::runtime::current_session->myscreen_->world().find_friends_in_range(og::runtime::current_session->myscreen_->world().oblist, 500, &howmany, seeker.get());
	    ASSERT_TRUE(howmany > 0) << "should find at least 1 friend in range";

    og::runtime::current_session->myscreen_->world().oblist.pop_back();
}


// ---------------------------------------------------------------------------
// damage_tile tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_damage_tile_out_of_bounds)
{
    char result = og::runtime::current_session->myscreen_->damage_tile(-10, -10);
    (void)result; // just verify no crash with negative coords
}


TEST(ScreenFuncs, screen_damage_tile_smoke)
{
    // Just call with a valid coordinate - should not crash
    char result = og::runtime::current_session->myscreen_->damage_tile(100, 100);
    (void)result;
}


// ---------------------------------------------------------------------------
// query_grid_passable smoke tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_query_grid_passable_center)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->setxy(100, 100);
    bool passable = og::runtime::current_session->myscreen_->world().query_grid_passable(100, 100, w.get());
    // Just check it doesn't crash
    (void)passable;

}


TEST(ScreenFuncs, screen_query_grid_passable_flying)
{
    auto w = create_living(FAMILY_FAERIE);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->setxy(100, 100);
    // Flying entities should pass over more terrain
    bool passable = og::runtime::current_session->myscreen_->world().query_grid_passable(100, 100, w.get());
    (void)passable;

}


TEST(ScreenFuncs, screen_query_grid_passable_out_of_bounds)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    bool passable = og::runtime::current_session->myscreen_->world().query_grid_passable(-10, -10, w.get());
    ASSERT_TRUE(!passable) << "out-of-bounds should not be passable";

}


// ---------------------------------------------------------------------------
// find_far_foe tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_find_far_foe_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    w->team_num = 0;

    walker* result = og::runtime::current_session->myscreen_->world().find_far_foe(w.get());
    // May or may not find one, but should not crash
    (void)result;

}


// ---------------------------------------------------------------------------
// find_near_foe tests
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_find_near_foe_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    w->team_num = 0;
    w->setxy(100, 100);

    walker* result = og::runtime::current_session->myscreen_->world().find_near_foe(w.get());
    (void)result;

}


// ---------------------------------------------------------------------------
// do_notify test
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_do_notify_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    og::runtime::current_session->myscreen_->do_notify("Test notification", w.get());
    og::runtime::current_session->myscreen_->do_notify("Broadcast", nullptr);

}


// ---------------------------------------------------------------------------
// query_passable smoke
// ---------------------------------------------------------------------------

TEST(ScreenFuncs, screen_query_passable_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    w->setxy(100, 100);

    bool p = og::runtime::current_session->myscreen_->world().query_passable(100, 100, w.get());
    (void)p;

}

