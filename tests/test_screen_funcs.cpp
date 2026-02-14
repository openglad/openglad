#include <openglad/legacy/graph.h>
#include <openglad/data/gloader.h>
#include <openglad/entities/guy.h>
#include "test_framework.h"
#include <memory>

extern screen* myscreen;

static std::unique_ptr<walker> create_living(char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l) return nullptr;
    auto w = l->create_walker_owned(Order::Living, family, myscreen);
    if (!w) return nullptr;
    w->setxy(50, 50);
    return w;
}

// ---------------------------------------------------------------------------
// first_of tests
// ---------------------------------------------------------------------------

void test_screen_first_of_found()
{
    auto w = create_living(FAMILY_SOLDIER);
	    TEST_ASSERT(w != nullptr, "create_walker should succeed");

	    // Add to oblist
	    myscreen->level_data.oblist.push_back(std::move(w));

    walker* found = myscreen->first_of(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(found != nullptr, "first_of should find the soldier");
    TEST_ASSERT_EQ((int)FAMILY_SOLDIER, (int)found->query_family(), "found should be soldier");

    // Remove from oblist (don't double-delete)
    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_screen_first_of_found);

void test_screen_first_of_not_found()
{
    walker* found = myscreen->first_of(Order::Living, FAMILY_ARCHMAGE, 99);
    // May or may not find one depending on test state, but should not crash
    (void)found;
}
REGISTER_TEST(test_screen_first_of_not_found);

void test_screen_first_of_with_team()
{
    auto w = create_living(FAMILY_SOLDIER);
	    TEST_ASSERT(w != nullptr, "create_walker should succeed");
	    w->team_num = 7;

	    myscreen->level_data.oblist.push_back(std::move(w));

    walker* found = myscreen->first_of(Order::Living, FAMILY_SOLDIER, 7);
    TEST_ASSERT(found != nullptr, "first_of with matching team should find it");

    walker* not_found = myscreen->first_of(Order::Living, FAMILY_SOLDIER, 99);
    TEST_ASSERT(not_found == nullptr || not_found->team_num != 99,
                "first_of with wrong team should not find team 99 unit");

    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_screen_first_of_with_team);

// ---------------------------------------------------------------------------
// find_in_range tests
// ---------------------------------------------------------------------------

void test_screen_find_in_range_basic()
{
    auto seeker = create_living(FAMILY_SOLDIER);
    auto target = create_living(FAMILY_MAGE);
    TEST_ASSERT(seeker != nullptr, "create seeker should succeed");
    TEST_ASSERT(target != nullptr, "create target should succeed");

	    seeker->setxy(100, 100);
	    target->setxy(110, 100);

	    myscreen->level_data.oblist.push_back(std::move(target));

	    Sint32 howmany = 0;
	    auto result = myscreen->find_in_range(myscreen->level_data.oblist, 500, &howmany, seeker.get());
	    TEST_ASSERT(howmany > 0, "should find at least 1 in range");

    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_screen_find_in_range_basic);

void test_screen_find_in_range_out_of_range()
{
    auto seeker = create_living(FAMILY_SOLDIER);
    auto target = create_living(FAMILY_MAGE);
    TEST_ASSERT(seeker != nullptr, "create seeker should succeed");
    TEST_ASSERT(target != nullptr, "create target should succeed");

	    seeker->setxy(50, 50);
	    target->setxy(250, 250);

	    myscreen->level_data.oblist.push_back(std::move(target));

	    Sint32 howmany = 0;
	    auto result = myscreen->find_in_range(myscreen->level_data.oblist, 5, &howmany, seeker.get());
	    (void)result; // range semantics vary; just verify no crash

    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_screen_find_in_range_out_of_range);

// ---------------------------------------------------------------------------
// find_foes_in_range tests
// ---------------------------------------------------------------------------

void test_screen_find_foes_in_range()
{
    auto seeker = create_living(FAMILY_SOLDIER);
    auto enemy = create_living(FAMILY_SMALL_SLIME);
    TEST_ASSERT(seeker != nullptr, "create seeker should succeed");
    TEST_ASSERT(enemy != nullptr, "create enemy should succeed");

    seeker->team_num = 0;
    seeker->setxy(100, 100);
	    enemy->team_num = 1;
	    enemy->setxy(110, 100);

	    myscreen->level_data.oblist.push_back(std::move(enemy));

	    Sint32 howmany = 0;
	    auto result = myscreen->find_foes_in_range(myscreen->level_data.oblist, 500, &howmany, seeker.get());
	    TEST_ASSERT(howmany > 0, "should find at least 1 foe in range");

    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_screen_find_foes_in_range);

// ---------------------------------------------------------------------------
// find_friends_in_range tests
// ---------------------------------------------------------------------------

void test_screen_find_friends_in_range()
{
    auto seeker = create_living(FAMILY_SOLDIER);
    auto friend_w = create_living(FAMILY_ARCHER);
    TEST_ASSERT(seeker != nullptr, "create seeker should succeed");
    TEST_ASSERT(friend_w != nullptr, "create friend should succeed");

    seeker->team_num = 0;
    seeker->setxy(100, 100);
	    friend_w->team_num = 0;
	    friend_w->setxy(110, 100);

	    myscreen->level_data.oblist.push_back(std::move(friend_w));

	    Sint32 howmany = 0;
	    auto result = myscreen->find_friends_in_range(myscreen->level_data.oblist, 500, &howmany, seeker.get());
	    TEST_ASSERT(howmany > 0, "should find at least 1 friend in range");

    myscreen->level_data.oblist.pop_back();
}
REGISTER_TEST(test_screen_find_friends_in_range);

// ---------------------------------------------------------------------------
// damage_tile tests
// ---------------------------------------------------------------------------

void test_screen_damage_tile_out_of_bounds()
{
    char result = myscreen->damage_tile(-10, -10);
    (void)result; // just verify no crash with negative coords
}
REGISTER_TEST(test_screen_damage_tile_out_of_bounds);

void test_screen_damage_tile_smoke()
{
    // Just call with a valid coordinate - should not crash
    char result = myscreen->damage_tile(100, 100);
    (void)result;
}
REGISTER_TEST(test_screen_damage_tile_smoke);

// ---------------------------------------------------------------------------
// query_grid_passable smoke tests
// ---------------------------------------------------------------------------

void test_screen_query_grid_passable_center()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    w->setxy(100, 100);
    bool passable = myscreen->query_grid_passable(100, 100, w.get());
    // Just check it doesn't crash
    (void)passable;

}
REGISTER_TEST(test_screen_query_grid_passable_center);

void test_screen_query_grid_passable_flying()
{
    auto w = create_living(FAMILY_FAERIE);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    w->setxy(100, 100);
    // Flying entities should pass over more terrain
    bool passable = myscreen->query_grid_passable(100, 100, w.get());
    (void)passable;

}
REGISTER_TEST(test_screen_query_grid_passable_flying);

void test_screen_query_grid_passable_out_of_bounds()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    bool passable = myscreen->query_grid_passable(-10, -10, w.get());
    TEST_ASSERT(!passable, "out-of-bounds should not be passable");

}
REGISTER_TEST(test_screen_query_grid_passable_out_of_bounds);

// ---------------------------------------------------------------------------
// find_far_foe tests
// ---------------------------------------------------------------------------

void test_screen_find_far_foe_smoke()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    w->team_num = 0;

    walker* result = myscreen->find_far_foe(w.get());
    // May or may not find one, but should not crash
    (void)result;

}
REGISTER_TEST(test_screen_find_far_foe_smoke);

// ---------------------------------------------------------------------------
// find_near_foe tests
// ---------------------------------------------------------------------------

void test_screen_find_near_foe_smoke()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    w->team_num = 0;
    w->setxy(100, 100);

    walker* result = myscreen->find_near_foe(w.get());
    (void)result;

}
REGISTER_TEST(test_screen_find_near_foe_smoke);

// ---------------------------------------------------------------------------
// do_notify test
// ---------------------------------------------------------------------------

void test_screen_do_notify_smoke()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");

    myscreen->do_notify("Test notification", w.get());
    myscreen->do_notify("Broadcast", nullptr);

}
REGISTER_TEST(test_screen_do_notify_smoke);

// ---------------------------------------------------------------------------
// query_passable smoke
// ---------------------------------------------------------------------------

void test_screen_query_passable_smoke()
{
    auto w = create_living(FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "create_walker should succeed");
    w->setxy(100, 100);

    bool p = myscreen->query_passable(100, 100, w.get());
    (void)p;

}
REGISTER_TEST(test_screen_query_passable_smoke);
