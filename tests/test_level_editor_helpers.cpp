#include "graph.h"
#include "gloader.h"
#include "io.h"
#include "pixdefs.h"
#include "test_framework.h"

#include <string>
#include <set>
#include <cstdio>
#include <unistd.h>

extern screen* myscreen;

// From level_editor.cpp
void set_screen_pos(screen* myscreen, Sint32 x, Sint32 y);
char get_random_matching_tile(Sint32 whatback);
Sint32 check_collide(Sint32 x, Sint32 y, Sint32 xsize, Sint32 ysize,
                     Sint32 x2, Sint32 y2, Sint32 xsize2, Sint32 ysize2);
walker* some_hit(Sint32 x, Sint32 y, walker* ob, LevelData* data);
bool create_new_campaign(const std::string& campaign_id);
bool does_campaign_exist(const std::string& campaign_id);
bool are_objects_outside_area(LevelData* level, int x, int y, int w, int h);
void get_connected_level_exits(int current_level, const std::list<int>& levels, std::set<int>& connected, std::list<std::string>& problems);
std::string get_editor_family_label(Order order, char family, char livings[][20], const char* treasures[], const char* weapons[]);
std::string get_editor_level_label(Order order, char family, int level);

static bool in_set(unsigned char v, unsigned char a, unsigned char b, unsigned char c, unsigned char d)
{
    return v == a || v == b || v == c || v == d;
}

void test_level_editor_set_screen_pos_and_tile_matching()
{
    set_screen_pos(myscreen, 123, -45);
    TEST_ASSERT_EQ(123, (int)myscreen->level_data.topx, "set_screen_pos should update topx");
    TEST_ASSERT_EQ(-45, (int)myscreen->level_data.topy, "set_screen_pos should update topy");

    unsigned char t = (unsigned char)get_random_matching_tile(PIX_GRASS1);
    TEST_ASSERT(in_set(t, PIX_GRASS1, PIX_GRASS2, PIX_GRASS3, PIX_GRASS4), "grass variant should be one of grass tiles");

    t = (unsigned char)get_random_matching_tile(PIX_GRASS_DARK_1);
    TEST_ASSERT(in_set(t, PIX_GRASS_DARK_1, PIX_GRASS_DARK_2, PIX_GRASS_DARK_3, PIX_GRASS_DARK_4), "dark grass variant should be one of dark grass tiles");

    t = (unsigned char)get_random_matching_tile(PIX_WATER1);
    TEST_ASSERT(t == PIX_WATER1 || t == PIX_WATER2 || t == PIX_WATER3, "water variant should be one of water tiles");

    t = (unsigned char)get_random_matching_tile(PIX_PAVEMENT1);
    TEST_ASSERT(t == PIX_PAVEMENT1 || t == PIX_PAVEMENT2 || t == PIX_PAVEMENT3, "pavement variant should be one of pavement tiles");

    // Default case should return original.
    t = (unsigned char)get_random_matching_tile(222);
    TEST_ASSERT_EQ(222, (int)t, "unknown tiles should be returned unchanged");

    // Label helpers extracted from editor rendering path.
    char test_livings[NUM_FAMILIES][20] = {};
    const char* test_treasures[NUM_FAMILIES] = {};
    const char* test_weapons[NUM_FAMILIES] = {};
    for (int i = 0; i < NUM_FAMILIES; ++i) {
        test_treasures[i] = "TREASURE";
        test_weapons[i] = "WEAPON";
    }
    snprintf(test_livings[FAMILY_SOLDIER], sizeof(test_livings[FAMILY_SOLDIER]), "SOLDIER");

    TEST_ASSERT(get_editor_family_label(Order::Generator, FAMILY_TENT, test_livings, test_treasures, test_weapons) == "TENT", "generator tent label");
    TEST_ASSERT(get_editor_family_label(Order::Generator, FAMILY_TOWER, test_livings, test_treasures, test_weapons) == "MAGE TOWER", "generator tower label");
    TEST_ASSERT(get_editor_family_label(Order::Generator, FAMILY_BONES, test_livings, test_treasures, test_weapons) == "BONEPILE", "generator bones label");
    TEST_ASSERT(get_editor_family_label(Order::Generator, FAMILY_TREEHOUSE, test_livings, test_treasures, test_weapons) == "TREEHOUSE", "generator treehouse label");
    TEST_ASSERT(get_editor_family_label(Order::Special, 0, test_livings, test_treasures, test_weapons) == "START TILE", "special start-tile label");
    TEST_ASSERT(get_editor_family_label(Order::Living, FAMILY_SOLDIER, test_livings, test_treasures, test_weapons) == "SOLDIER", "living label");
    TEST_ASSERT(get_editor_family_label(Order::Treasure, FAMILY_KEY, test_livings, test_treasures, test_weapons) == "TREASURE", "treasure family label");
    TEST_ASSERT(get_editor_family_label(Order::Weapon, FAMILY_DOOR, test_livings, test_treasures, test_weapons) == "WEAPON", "weapon family label");
    TEST_ASSERT(get_editor_family_label(static_cast<Order>(255), 0, test_livings, test_treasures, test_weapons) == "UNKNOWN", "unknown order label fallback");

    TEST_ASSERT(get_editor_level_label(Order::Living, FAMILY_SOLDIER, 7) == "LEVEL: 7", "living level label");
    TEST_ASSERT(get_editor_level_label(Order::Generator, FAMILY_TOWER, 3) == "LEVEL: 3", "generator level label");
    TEST_ASSERT(get_editor_level_label(Order::Treasure, FAMILY_GOLD_BAR, 50) == "VALUE: 50", "gold value label");
    TEST_ASSERT(get_editor_level_label(Order::Treasure, FAMILY_KEY, 4) == "DOOR ID: 4", "key door label");
    TEST_ASSERT(get_editor_level_label(Order::Treasure, FAMILY_TELEPORTER, 2) == "GROUP: 2", "teleporter group label");
    TEST_ASSERT(get_editor_level_label(Order::Treasure, FAMILY_EXIT, 9) == "EXIT TO: 9", "exit destination label");
    TEST_ASSERT(get_editor_level_label(Order::Treasure, FAMILY_MAGIC_POTION, 5) == "POWER: 5", "treasure power label");
    TEST_ASSERT(get_editor_level_label(Order::Treasure, FAMILY_STAIN, 1).empty(), "stain has no level label");
    TEST_ASSERT(get_editor_level_label(Order::Weapon, FAMILY_DOOR, 6) == "DOOR ID: 6", "weapon door label");
    TEST_ASSERT(get_editor_level_label(Order::Weapon, FAMILY_KNIFE, 8) == "POWER: 8", "weapon power label");
    TEST_ASSERT(get_editor_level_label(static_cast<Order>(255), FAMILY_KNIFE, 8).empty(), "unknown order has empty level label");
}
REGISTER_TEST(test_level_editor_set_screen_pos_and_tile_matching);

void test_level_editor_check_collide_quadrants()
{
    TEST_ASSERT_EQ(1, (int)check_collide(0, 0, 10, 10, 5, 5, 3, 3), "overlap should collide");
    TEST_ASSERT_EQ(0, (int)check_collide(0, 0, 10, 10, 20, 20, 3, 3), "separated should not collide");
    TEST_ASSERT_EQ(1, (int)check_collide(10, 10, 5, 5, 8, 8, 10, 10), "overlap with x>=x2,y>=y2 should collide");
    TEST_ASSERT_EQ(1, (int)check_collide(10, 5, 5, 5, 8, 8, 10, 10), "overlap with x>=x2,y<y2 should collide");
    TEST_ASSERT_EQ(1, (int)check_collide(5, 10, 5, 5, 8, 8, 10, 10), "overlap with x<x2,y>=y2 should collide");
}
REGISTER_TEST(test_level_editor_check_collide_quadrants);

struct ListsSwap {
    std::list<std::unique_ptr<walker>> saved_ob, saved_fx, saved_weap;
    ListsSwap()
    {
        saved_ob.splice(saved_ob.end(), myscreen->level_data.oblist);
        saved_fx.splice(saved_fx.end(), myscreen->level_data.fxlist);
        saved_weap.splice(saved_weap.end(), myscreen->level_data.weaplist);
    }
    ~ListsSwap()
    {
        myscreen->level_data.oblist.splice(myscreen->level_data.oblist.end(), saved_ob);
        myscreen->level_data.fxlist.splice(myscreen->level_data.fxlist.end(), saved_fx);
        myscreen->level_data.weaplist.splice(myscreen->level_data.weaplist.end(), saved_weap);
    }
};

static walker* make_living(unsigned char family)
{
    loader* l = myscreen->level_data.myloader.get();
    if (!l)
        return nullptr;
    walker* w = l->create_walker(Order::Living, family, myscreen);
    if (w) {
        w->dead = 0;
        w->user = -1;
    }
    return w;
}

void test_level_editor_some_hit_checks_all_lists()
{
    ListsSwap swap;

    walker* probe = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(probe != nullptr, "probe should be created");
    probe->setxy(10, 10);

    // oblist hit
    walker* target1 = make_living(FAMILY_ORC);
    TEST_ASSERT(target1 != nullptr, "target1 should be created");
    target1->setxy(10, 10);
    myscreen->level_data.oblist.push_back(std::unique_ptr<walker>(target1));

    walker* hit = some_hit(10, 10, probe, &myscreen->level_data);
    TEST_ASSERT(hit == target1, "some_hit should find hit in oblist");
    TEST_ASSERT(probe->collide_ob == target1, "collide_ob should be set");

    myscreen->level_data.oblist.clear();

    // fxlist hit
    walker* target2 = make_living(FAMILY_ORC);
    target2->setxy(10, 10);
    myscreen->level_data.fxlist.push_back(std::unique_ptr<walker>(target2));
    hit = some_hit(10, 10, probe, &myscreen->level_data);
    TEST_ASSERT(hit == target2, "some_hit should find hit in fxlist");

    myscreen->level_data.fxlist.clear();

    // weaplist hit
    walker* target3 = make_living(FAMILY_ORC);
    target3->setxy(10, 10);
    myscreen->level_data.weaplist.push_back(std::unique_ptr<walker>(target3));
    hit = some_hit(10, 10, probe, &myscreen->level_data);
    TEST_ASSERT(hit == target3, "some_hit should find hit in weaplist");

    myscreen->level_data.weaplist.clear();

    // no hit
    hit = some_hit(1000, 1000, probe, &myscreen->level_data);
    TEST_ASSERT(hit == nullptr, "some_hit should return null when no overlap");
    TEST_ASSERT(probe->collide_ob == nullptr, "collide_ob should be cleared on miss");

    delete probe;
}
REGISTER_TEST(test_level_editor_some_hit_checks_all_lists);

void test_level_editor_create_new_campaign_and_detect_exists()
{
    std::string id = std::string("org.openglad.test.") + std::to_string(::getpid());
    // Ensure a clean slate.
    delete_campaign(id);

    TEST_ASSERT(!does_campaign_exist(id), "campaign should not exist before create");
    TEST_ASSERT(create_new_campaign(id), "create_new_campaign should succeed");
    TEST_ASSERT(does_campaign_exist(id), "campaign should exist after create");

    // Exercise recursive graph traversal over level exits in a real campaign.
    std::list<int> levels{1, 2, 3, 4, 5};
    std::set<int> connected;
    std::list<std::string> problems;
    get_connected_level_exits(1, levels, connected, problems);
    TEST_ASSERT(connected.find(1) != connected.end(), "connected set should include starting level");

    // Error path for missing level id.
    std::set<int> missing_connected;
    std::list<std::string> missing_problems;
    get_connected_level_exits(9999, levels, missing_connected, missing_problems);
    TEST_ASSERT(!missing_problems.empty(), "missing level should report at least one problem");

    // Area bounds checks for inside/outside object detection.
    myscreen->level_data.create_new_grid();
    walker* inside = myscreen->level_data.add_ob(Order::Living, FAMILY_SOLDIER);
    walker* outside = myscreen->level_data.add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(inside != nullptr && outside != nullptr, "test objects should be created");
    if (inside && outside) {
        inside->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
        outside->setxy(GRID_SIZE * 20, GRID_SIZE * 20);
        TEST_ASSERT(are_objects_outside_area(&myscreen->level_data, 0, 0, 10, 10),
                    "outside object should be detected");
        outside->setxy(GRID_SIZE * 3, GRID_SIZE * 3);
        TEST_ASSERT(!are_objects_outside_area(&myscreen->level_data, 0, 0, 10, 10),
                    "all objects inside area should report false");

        loader* l = myscreen->level_data.myloader.get();
        walker* fx_inside = l ? l->create_walker(Order::FX, FAMILY_FLASH, myscreen) : nullptr;
        walker* fx_outside = l ? l->create_walker(Order::FX, FAMILY_FLASH, myscreen) : nullptr;
        TEST_ASSERT(fx_inside != nullptr && fx_outside != nullptr, "fx objects should be created");
        if (fx_inside && fx_outside) {
            myscreen->level_data.fxlist.push_back(std::unique_ptr<walker>(fx_inside));
            myscreen->level_data.fxlist.push_back(std::unique_ptr<walker>(fx_outside));
            fx_inside->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            fx_outside->setxy(GRID_SIZE * 20, GRID_SIZE * 20);
            TEST_ASSERT(are_objects_outside_area(&myscreen->level_data, 0, 0, 10, 10),
                        "outside fx object should be detected");
            fx_outside->setxy(GRID_SIZE * 3, GRID_SIZE * 3);
            TEST_ASSERT(!are_objects_outside_area(&myscreen->level_data, 0, 0, 10, 10),
                        "inside fx objects should report false");
        }

        walker* weap_inside = l ? l->create_walker(Order::Weapon, FAMILY_BOMB, myscreen) : nullptr;
        walker* weap_outside = l ? l->create_walker(Order::Weapon, FAMILY_BOMB, myscreen) : nullptr;
        TEST_ASSERT(weap_inside != nullptr && weap_outside != nullptr, "weapon objects should be created");
        if (weap_inside && weap_outside) {
            myscreen->level_data.weaplist.push_back(std::unique_ptr<walker>(weap_inside));
            myscreen->level_data.weaplist.push_back(std::unique_ptr<walker>(weap_outside));
            weap_inside->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            weap_outside->setxy(GRID_SIZE * 20, GRID_SIZE * 20);
            TEST_ASSERT(are_objects_outside_area(&myscreen->level_data, 0, 0, 10, 10),
                        "outside weapon object should be detected");
            weap_outside->setxy(GRID_SIZE * 3, GRID_SIZE * 3);
            TEST_ASSERT(!are_objects_outside_area(&myscreen->level_data, 0, 0, 10, 10),
                        "inside weapon objects should report false");
        }
    }
    myscreen->level_data.delete_objects();

    delete_campaign(id);
}
REGISTER_TEST(test_level_editor_create_new_campaign_and_detect_exists);
