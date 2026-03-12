#include <openglad/gameplay/guy.h>
#include <openglad/resources/gloader.h>
#include <openglad/gameplay/statistics.h>
#include <openglad/gameplay/walker.h>
#include <openglad/legacy/base.h>
#include <openglad/interface/render/view.h>
#include <openglad/interface/screen.h>
#include "test_framework.h"
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
// facing tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_facing_right)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    short dir = w->facing(10, 0);
    ASSERT_EQ(FACE_RIGHT, (int)dir) << "facing right should be FACE_RIGHT";

}


TEST(WalkerExtended, walker_facing_left)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    short dir = w->facing(-10, 0);
    ASSERT_EQ(FACE_LEFT, (int)dir) << "facing left should be FACE_LEFT";

}


TEST(WalkerExtended, walker_facing_up)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    short dir = w->facing(0, -10);
    ASSERT_EQ(FACE_UP, (int)dir) << "facing up should be FACE_UP";

}


TEST(WalkerExtended, walker_facing_down)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    short dir = w->facing(0, 10);
    ASSERT_EQ(FACE_DOWN, (int)dir) << "facing down should be FACE_DOWN";

}


TEST(WalkerExtended, walker_facing_all_directions)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    // Test all 8 cardinal directions
    w->facing(10, 0);     // right
    w->facing(-10, 0);    // left
    w->facing(0, -10);    // up
    w->facing(0, 10);     // down
    w->facing(10, -10);   // up-right
    w->facing(-10, -10);  // up-left
    w->facing(10, 10);    // down-right
    w->facing(-10, 10);   // down-left

}


// ---------------------------------------------------------------------------
// turn tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_turn_basic)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->curdir = FACE_UP;
    w->turn(FACE_RIGHT);
    // After turning, curdir should have changed
    ASSERT_TRUE(w->curdir != FACE_UP || w->curdir == FACE_RIGHT) << "turn should change direction";

}


TEST(WalkerExtended, walker_turn_all_targets)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = FACE_UP;
        w->turn(static_cast<short>(dir));
    }

}


// ---------------------------------------------------------------------------
// distance_to_ob tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_distance_to_self)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    Sint32 d = w->distance_to_ob(w.get());
    ASSERT_EQ(0, (int)d) << "distance to self should be 0";

}


TEST(WalkerExtended, walker_distance_to_other)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_MAGE);
    ASSERT_TRUE(a != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(b != nullptr) << "create_walker should succeed";

    a->setxy(100, 100);
    b->setxy(110, 100);

    Sint32 d = a->distance_to_ob(b.get());
    ASSERT_TRUE(d > 0) << "distance to nearby unit should be positive";
    ASSERT_TRUE(d < 50) << "distance to 10px away should be < 50";

}


TEST(WalkerExtended, walker_distance_to_ob_center)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_MAGE);
    ASSERT_TRUE(a != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(b != nullptr) << "create_walker should succeed";

    a->setxy(100, 100);
    b->setxy(110, 100);

    Sint32 d = a->distance_to_ob_center(b.get());
    ASSERT_TRUE(d > 0) << "center distance should be positive";

}


// ---------------------------------------------------------------------------
// query_team_color test
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_query_team_color)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->team_num = 0;
    unsigned char c0 = w->query_team_color();
    ASSERT_EQ(40, (int)c0) << "team 0 color should be 40";

    w->team_num = 1;
    unsigned char c1 = w->query_team_color();
    ASSERT_EQ(56, (int)c1) << "team 1 color should be 56";

    w->team_num = 3;
    unsigned char c3 = w->query_team_color();
    ASSERT_EQ(88, (int)c3) << "team 3 color should be 88";

}


// ---------------------------------------------------------------------------
// get_current_angle test
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_get_current_angle)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    for (int dir = 0; dir < 8; dir++) {
        w->curdir = static_cast<char>(dir);
        float angle = w->get_current_angle();
        (void)angle; // just verify no crash
    }

}


// ---------------------------------------------------------------------------
// act_type tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_set_act_type)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->set_act_type(ACT_CONTROL);
    ASSERT_EQ(ACT_CONTROL, (int)w->act_type) << "act type should be ACT_CONTROL";

    w->old_act_type = ACT_RANDOM;
    ASSERT_EQ(ACT_RANDOM, (int)w->old_act_type) << "old act type should be ACT_RANDOM";

    w->restore_act_type();
    ASSERT_EQ(ACT_RANDOM, (int)w->act_type) << "restored act type should be ACT_RANDOM";

}


// ---------------------------------------------------------------------------
// collide test
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_collide)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_MAGE);
    ASSERT_TRUE(a != nullptr) << "create_walker should succeed";
    ASSERT_TRUE(b != nullptr) << "create_walker should succeed";

    bool r = a->collide(b.get());
    ASSERT_TRUE(r) << "collide should return true";

}


// ---------------------------------------------------------------------------
// walk / walkstep smoke tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_walk_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    w->setxy(100, 100);

    w->walk(1, 0);
    w->walk(0, 1);
    w->walk(-1, 0);
    w->walk(0, -1);
    w->walk(0, 0); // special case

}


TEST(WalkerExtended, walker_walkstep_smoke)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";
    w->setxy(100, 100);

    w->walkstep(1, 0);
    w->walkstep(0, 1);
    w->walkstep(-1, -1);

}


// ---------------------------------------------------------------------------
// set_order_family test
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_set_order_family)
{
    auto w = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(w != nullptr) << "create_walker should succeed";

    w->set_order_family(Order::Living, FAMILY_ARCHER);
    ASSERT_EQ((int)FAMILY_ARCHER, (int)w->family) << "family should be archer";

}


// ---------------------------------------------------------------------------
// is_friendly extended tests
// ---------------------------------------------------------------------------

TEST(WalkerExtended, walker_is_friendly_same_team)
{
    auto a = create_living(FAMILY_SOLDIER);
    auto b = create_living(FAMILY_ARCHER);
    ASSERT_TRUE(a != nullptr) << "create a should succeed";
    ASSERT_TRUE(b != nullptr) << "create b should succeed";

    a->team_num = 0;
    b->team_num = 0;
    ASSERT_TRUE(a->is_friendly(b.get())) << "same team should be friendly";

}


TEST(WalkerExtended, walker_is_friendly_null)
{
    auto a = create_living(FAMILY_SOLDIER);
    ASSERT_TRUE(a != nullptr) << "create a should succeed";

    ASSERT_TRUE(!a->is_friendly(nullptr)) << "null target should not be friendly";

}

