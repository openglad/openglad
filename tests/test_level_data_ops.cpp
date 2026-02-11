#include "graph.h"
#include "guy.h"
#include "gloader.h"
#include "test_framework.h"

extern screen* myscreen;

// ---------------------------------------------------------------------------
// LevelData::clear
// ---------------------------------------------------------------------------

void test_level_data_clear()
{
    myscreen->level_data.title = "Modified";
    myscreen->level_data.type = 5;
    myscreen->level_data.par_value = 99;
    myscreen->level_data.time_bonus_limit = 9999;
    myscreen->level_data.topx = 50;
    myscreen->level_data.topy = 50;

    myscreen->level_data.clear();

    TEST_ASSERT(myscreen->level_data.title == "New Level", "title reset");
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.type, "type reset");
    TEST_ASSERT_EQ(1, (int)myscreen->level_data.par_value, "par_value reset");
    TEST_ASSERT_EQ(4000, (int)myscreen->level_data.time_bonus_limit, "time_bonus_limit reset");
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.topx, "topx reset");
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.topy, "topy reset");
    TEST_ASSERT_EQ(0, (int)myscreen->level_data.numobs, "numobs reset");
    TEST_ASSERT(myscreen->level_data.oblist.empty(), "oblist reset");
    TEST_ASSERT(myscreen->level_data.fxlist.empty(), "fxlist reset");
    TEST_ASSERT(myscreen->level_data.weaplist.empty(), "weaplist reset");

    // Restore grid for other tests
    myscreen->level_data.create_new_grid();
}
REGISTER_TEST(test_level_data_clear);

// ---------------------------------------------------------------------------
// LevelData::create_new_grid
// ---------------------------------------------------------------------------

void test_level_data_create_new_grid()
{
    myscreen->level_data.create_new_grid();
    TEST_ASSERT(myscreen->level_data.grid.valid(), "grid should be valid");
    TEST_ASSERT_EQ(40, (int)myscreen->level_data.grid.w, "grid width 40");
    TEST_ASSERT_EQ(60, (int)myscreen->level_data.grid.h, "grid height 60");
    TEST_ASSERT(myscreen->level_data.pixmaxx > 0, "pixmaxx positive");
    TEST_ASSERT(myscreen->level_data.pixmaxy > 0, "pixmaxy positive");
    TEST_ASSERT(myscreen->level_data.grid.data != nullptr, "grid data allocated");

    // Grass tile generation should stay in expected range.
    for (int i = 0; i < 25; i++)
    {
        unsigned char t = myscreen->level_data.grid.data[i];
        TEST_ASSERT(t >= PIX_GRASS1 && t <= PIX_GRASS4, "new grid tiles should be grass variants");
    }
}
REGISTER_TEST(test_level_data_create_new_grid);

// ---------------------------------------------------------------------------
// LevelData::resize_grid
// ---------------------------------------------------------------------------

void test_level_data_resize_grid_grow()
{
    myscreen->level_data.create_new_grid();
    int old_w = myscreen->level_data.grid.w;
    int old_h = myscreen->level_data.grid.h;
    unsigned char old00 = myscreen->level_data.grid.data[0];

    myscreen->level_data.resize_grid(50, 70);
    TEST_ASSERT_EQ(50, (int)myscreen->level_data.grid.w, "resized width");
    TEST_ASSERT_EQ(70, (int)myscreen->level_data.grid.h, "resized height");
    TEST_ASSERT_EQ((int)old00, (int)myscreen->level_data.grid.data[0], "existing cells should be preserved");
    (void)old_w;
    (void)old_h;

    // Restore
    myscreen->level_data.resize_grid(40, 60);
}
REGISTER_TEST(test_level_data_resize_grid_grow);

void test_level_data_resize_grid_shrink()
{
    myscreen->level_data.create_new_grid();
    myscreen->level_data.resize_grid(20, 30);
    TEST_ASSERT_EQ(20, (int)myscreen->level_data.grid.w, "shrunk width");
    TEST_ASSERT_EQ(30, (int)myscreen->level_data.grid.h, "shrunk height");

    // Restore
    myscreen->level_data.resize_grid(40, 60);
}
REGISTER_TEST(test_level_data_resize_grid_shrink);

void test_level_data_resize_grid_invalid()
{
    myscreen->level_data.create_new_grid();
    int w_before = myscreen->level_data.grid.w;

    myscreen->level_data.resize_grid(2, 2); // too small
    TEST_ASSERT_EQ(w_before, (int)myscreen->level_data.grid.w, "invalid resize should be no-op");

    myscreen->level_data.resize_grid(256, 256); // too large
    TEST_ASSERT_EQ(w_before, (int)myscreen->level_data.grid.w, "oversized resize should be no-op");
}
REGISTER_TEST(test_level_data_resize_grid_invalid);

// ---------------------------------------------------------------------------
// LevelData::add_ob / remove_ob
// ---------------------------------------------------------------------------

void test_level_data_add_ob_living()
{
    int obs_before = myscreen->level_data.numobs;
    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(w != nullptr, "add_ob living should succeed");
    TEST_ASSERT_EQ(obs_before + 1, myscreen->level_data.numobs, "numobs incremented");
    myscreen->level_data.remove_ob(w);
    TEST_ASSERT_EQ(obs_before, myscreen->level_data.numobs, "numobs decremented");
}
REGISTER_TEST(test_level_data_add_ob_living);

void test_level_data_add_ob_weapon()
{
    int obs_before = myscreen->level_data.numobs;
    walker* w = myscreen->level_data.add_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(w != nullptr, "add_ob weapon should succeed");
    TEST_ASSERT_EQ(obs_before, myscreen->level_data.numobs, "weapon should not increment numobs");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_level_data_add_ob_weapon);

void test_level_data_add_fx_ob()
{
    int obs_before = myscreen->level_data.numobs;
    walker* w = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    TEST_ASSERT(w != nullptr, "add_fx_ob should succeed");
    TEST_ASSERT_EQ(obs_before, myscreen->level_data.numobs, "fx should not increment numobs");
    myscreen->level_data.remove_ob(w);
}
REGISTER_TEST(test_level_data_add_fx_ob);

void test_level_data_add_weap_ob()
{
    walker* w = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(w != nullptr, "add_weap_ob should succeed");
    short result = myscreen->level_data.remove_ob(w);
    TEST_ASSERT_EQ(1, (int)result, "remove_ob should find weapon");
}
REGISTER_TEST(test_level_data_add_weap_ob);

void test_level_data_remove_ob_from_each_list()
{
    // Add to oblist (living)
    walker* living = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    TEST_ASSERT(living != nullptr, "living created");

    // Add to fxlist
    walker* fx = myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    TEST_ASSERT(fx != nullptr, "fx created");

    // Add to weaplist
    walker* weap = myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    TEST_ASSERT(weap != nullptr, "weap created");

    // Remove from each
    short r1 = myscreen->level_data.remove_ob(weap);
    TEST_ASSERT_EQ(1, (int)r1, "removed from weaplist");

    short r2 = myscreen->level_data.remove_ob(fx);
    TEST_ASSERT_EQ(1, (int)r2, "removed from fxlist");

    short r3 = myscreen->level_data.remove_ob(living);
    TEST_ASSERT_EQ(1, (int)r3, "removed from oblist");

    walker* non_member = myscreen->level_data.myloader->create_walker(Order::Living, FAMILY_SOLDIER, myscreen);
    TEST_ASSERT(non_member != nullptr, "non-member walker created");
    short r4 = myscreen->level_data.remove_ob(non_member);
    TEST_ASSERT_EQ(0, (int)r4, "removing non-member object should fail");
    delete non_member;
    short r5 = myscreen->level_data.remove_ob(nullptr);
    TEST_ASSERT_EQ(0, (int)r5, "removing null should fail");
}
REGISTER_TEST(test_level_data_remove_ob_from_each_list);

// ---------------------------------------------------------------------------
// LevelData::delete_objects
// ---------------------------------------------------------------------------

void test_level_data_delete_objects()
{
    // Add some objects
    myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    myscreen->level_data.add_ob(Order::Living, FAMILY_ARCHER);
    myscreen->level_data.add_fx_ob(Order::FX, FAMILY_EXPLOSION);
    myscreen->level_data.add_weap_ob(Order::Weapon, FAMILY_KNIFE);
    myscreen->level_data.dead_list.push_back(std::unique_ptr<walker>(myscreen->level_data.myloader->create_walker(Order::Living, FAMILY_ORC, myscreen)));

    myscreen->level_data.delete_objects();

    TEST_ASSERT(myscreen->level_data.oblist.empty(), "oblist empty");
    TEST_ASSERT(myscreen->level_data.fxlist.empty(), "fxlist empty");
    TEST_ASSERT(myscreen->level_data.weaplist.empty(), "weaplist empty");
    TEST_ASSERT(myscreen->level_data.dead_list.empty(), "dead_list empty");
    TEST_ASSERT_EQ(0, myscreen->level_data.numobs, "numobs 0");
}
REGISTER_TEST(test_level_data_delete_objects);

// ---------------------------------------------------------------------------
// LevelData::set_draw_pos / add_draw_pos
// ---------------------------------------------------------------------------

void test_level_data_set_draw_pos()
{
    myscreen->level_data.set_draw_pos(100, 200);
    TEST_ASSERT_EQ(100, (int)myscreen->level_data.topx, "topx set");
    TEST_ASSERT_EQ(200, (int)myscreen->level_data.topy, "topy set");

    myscreen->level_data.set_draw_pos(0, 0);
}
REGISTER_TEST(test_level_data_set_draw_pos);

void test_level_data_add_draw_pos()
{
    myscreen->level_data.set_draw_pos(100, 200);
    myscreen->level_data.add_draw_pos(10, 20);
    TEST_ASSERT_EQ(110, (int)myscreen->level_data.topx, "topx added");
    TEST_ASSERT_EQ(220, (int)myscreen->level_data.topy, "topy added");

    myscreen->level_data.add_draw_pos(-5, -10);
    TEST_ASSERT_EQ(105, (int)myscreen->level_data.topx, "topx supports negative deltas");
    TEST_ASSERT_EQ(210, (int)myscreen->level_data.topy, "topy supports negative deltas");

    myscreen->level_data.set_draw_pos(0, 0);
}
REGISTER_TEST(test_level_data_add_draw_pos);

// ---------------------------------------------------------------------------
// LevelData::get_description_line
// ---------------------------------------------------------------------------

void test_level_data_get_description_line()
{
    myscreen->level_data.description.clear();
    myscreen->level_data.description.push_back("Line 1");
    myscreen->level_data.description.push_back("Line 2");
    myscreen->level_data.description.push_back("Line 3");

    TEST_ASSERT(myscreen->level_data.get_description_line(0) == "Line 1", "line 0");
    TEST_ASSERT(myscreen->level_data.get_description_line(1) == "Line 2", "line 1");
    TEST_ASSERT(myscreen->level_data.get_description_line(2) == "Line 3", "line 2");
    TEST_ASSERT(myscreen->level_data.get_description_line(10) == "", "out of bounds returns empty");
    TEST_ASSERT(myscreen->level_data.get_description_line(-1) == "Line 1", "negative index returns first line");

    myscreen->level_data.description.clear();

    CampaignData c("org.openglad.tests");
    c.description.clear();
    c.description.push_back("Campaign 1");
    c.description.push_back("Campaign 2");
    TEST_ASSERT(c.getDescriptionLine(0) == "Campaign 1", "campaign line 0");
    TEST_ASSERT(c.getDescriptionLine(1) == "Campaign 2", "campaign line 1");
    TEST_ASSERT(c.getDescriptionLine(10) == "", "campaign out of range");
}
REGISTER_TEST(test_level_data_get_description_line);

// ---------------------------------------------------------------------------
// LevelData::resize_grid with objects - tests off-map cleanup
// ---------------------------------------------------------------------------

void test_level_data_resize_grid_removes_offmap()
{
    myscreen->level_data.create_new_grid();
    // Add an object far out
    walker* w = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    if (w) {
        w->setxy(500, 500); // way beyond 40*GRID_SIZE
        size_t before = myscreen->level_data.oblist.size();
        myscreen->level_data.resize_grid(10, 10);
        // Object at (500,500) should be removed from 10*GRID_SIZE grid
        TEST_ASSERT(myscreen->level_data.oblist.size() < before, "off-map objects removed");
    }
    // Restore
    myscreen->level_data.resize_grid(40, 60);
}
REGISTER_TEST(test_level_data_resize_grid_removes_offmap);
