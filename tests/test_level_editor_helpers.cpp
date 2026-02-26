#include <openglad/data/gloader.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/entities/walker.h>
#include <openglad/platform/io.h>
#include <openglad/legacy/pixdefs.h>
#include <openglad/runtime/screen.h>
#include <openglad/legacy/base.h>
#include "test_framework.h"

#include <string>
#include <set>
#include <cstdio>
#include <unistd.h>

// myscreen is now a macro defined in base.h (via game_session.h)

// From level_editor.cpp
void set_screen_pos(screen* scr, Sint32 x, Sint32 y);
char get_random_matching_tile(Sint32 whatback);
Sint32 check_collide(Sint32 x, Sint32 y, Sint32 xsize, Sint32 ysize,
                     Sint32 x2, Sint32 y2, Sint32 xsize2, Sint32 ysize2);
walker* some_hit(Sint32 x, Sint32 y, walker* ob, og::gameplay::GameWorld& world);
bool create_new_campaign(const std::string& campaign_id);
bool does_campaign_exist(const std::string& campaign_id);
bool are_objects_outside_area(const og::gameplay::GameWorld& world, int x, int y, int w, int h);
void get_connected_level_exits(int current_level, const std::list<int>& levels, std::set<int>& connected, std::list<std::string>& problems);
std::string get_editor_family_label(Order order, Sint32 family, char livings[][20], const char* treasures[], const char* weapons[]);
std::string get_editor_level_label(Order order, Sint32 family, Sint32 level);
void importCampaignPicker();
void shareCampaign(screen* scr);
bool prompt_for_string(const std::string& message, std::string& result);
int level_editor_test_exercise_internal_helpers();

static bool in_set(unsigned char v, unsigned char a, unsigned char b, unsigned char c, unsigned char d)
{
    return v == a || v == b || v == c || v == d;
}

void test_level_editor_set_screen_pos_and_tile_matching()
{
    set_screen_pos(og::runtime::current_session->myscreen_, 123, -45);
    TEST_ASSERT_EQ(123, (int)og::runtime::current_session->myscreen_->level_visuals_.topx, "set_screen_pos should update topx");
    TEST_ASSERT_EQ(-45, (int)og::runtime::current_session->myscreen_->level_visuals_.topy, "set_screen_pos should update topy");

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

    importCampaignPicker();
    shareCampaign(og::runtime::current_session->myscreen_);
    std::string name = "Default";
    TEST_ASSERT(prompt_for_string("Name", name), "prompt_for_string test-mode path should accept");
    TEST_ASSERT(level_editor_test_exercise_internal_helpers() > 0, "internal helper exerciser should run");

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
        saved_ob.splice(saved_ob.end(), og::runtime::current_session->myscreen_->world().oblist);
        saved_fx.splice(saved_fx.end(), og::runtime::current_session->myscreen_->world().fxlist);
        saved_weap.splice(saved_weap.end(), og::runtime::current_session->myscreen_->world().weaplist);
    }
    ~ListsSwap()
    {
        og::runtime::current_session->myscreen_->world().oblist.splice(og::runtime::current_session->myscreen_->world().oblist.end(), saved_ob);
        og::runtime::current_session->myscreen_->world().fxlist.splice(og::runtime::current_session->myscreen_->world().fxlist.end(), saved_fx);
        og::runtime::current_session->myscreen_->world().weaplist.splice(og::runtime::current_session->myscreen_->world().weaplist.end(), saved_weap);
    }
};

static std::unique_ptr<walker> make_living(unsigned char family)
{
    loader* l = og::runtime::current_session->myscreen_->myloader.get();
    if (!l)
        return nullptr;
    auto w = l->create_walker_owned(Order::Living, family);
    if (w) {
        w->dead = 0;
        w->user = -1;
    }
    return w;
}

void test_level_editor_some_hit_checks_all_lists()
{
    ListsSwap swap;
    og::gameplay::GameWorld& world = og::runtime::current_session->myscreen_->world();

    auto probe = make_living(FAMILY_SOLDIER);
    TEST_ASSERT(probe != nullptr, "probe should be created");
    walker* probep = probe.get();
    probep->setxy(10, 10);

    // oblist hit
    auto target1 = make_living(FAMILY_ORC);
    TEST_ASSERT(target1 != nullptr, "target1 should be created");
    walker* target1p = target1.get();
    target1p->setxy(10, 10);
    og::runtime::current_session->myscreen_->world().oblist.push_back(std::move(target1));

    walker* hit = some_hit(10, 10, probep, world);
    TEST_ASSERT(hit == target1p, "some_hit should find hit in oblist");
    TEST_ASSERT(probep->collide_ob == target1p, "collide_ob should be set");

    og::runtime::current_session->myscreen_->world().oblist.clear();

    // fxlist hit
    auto target2 = make_living(FAMILY_ORC);
    walker* target2p = target2.get();
    target2p->setxy(10, 10);
    og::runtime::current_session->myscreen_->world().fxlist.push_back(std::move(target2));
    hit = some_hit(10, 10, probep, world);
    TEST_ASSERT(hit == target2p, "some_hit should find hit in fxlist");

    og::runtime::current_session->myscreen_->world().fxlist.clear();

    // weaplist hit
    auto target3 = make_living(FAMILY_ORC);
    walker* target3p = target3.get();
    target3p->setxy(10, 10);
    og::runtime::current_session->myscreen_->world().weaplist.push_back(std::move(target3));
    hit = some_hit(10, 10, probep, world);
    TEST_ASSERT(hit == target3p, "some_hit should find hit in weaplist");

    og::runtime::current_session->myscreen_->world().weaplist.clear();

    // no hit
    hit = some_hit(1000, 1000, probep, world);
    TEST_ASSERT(hit == nullptr, "some_hit should return null when no overlap");
    TEST_ASSERT(probep->collide_ob == nullptr, "collide_ob should be cleared on miss");
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
    og::runtime::current_session->myscreen_->world().create_new_grid();
    walker* inside = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_SOLDIER);
    walker* outside = og::runtime::current_session->myscreen_->world().add_ob(Order::Living, FAMILY_ORC);
    TEST_ASSERT(inside != nullptr && outside != nullptr, "test objects should be created");
    if (inside && outside) {
        inside->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
        outside->setxy(GRID_SIZE * 20, GRID_SIZE * 20);
        TEST_ASSERT(are_objects_outside_area(og::runtime::current_session->myscreen_->world(), 0, 0, 10, 10),
                    "outside object should be detected");
        outside->setxy(GRID_SIZE * 3, GRID_SIZE * 3);
        TEST_ASSERT(!are_objects_outside_area(og::runtime::current_session->myscreen_->world(), 0, 0, 10, 10),
                    "all objects inside area should report false");

        loader* l = og::runtime::current_session->myscreen_->myloader.get();
        auto fx_inside = l ? l->create_walker_owned(Order::FX, FAMILY_FLASH) : nullptr;
        auto fx_outside = l ? l->create_walker_owned(Order::FX, FAMILY_FLASH) : nullptr;
        TEST_ASSERT(fx_inside != nullptr && fx_outside != nullptr, "fx objects should be created");
        if (fx_inside && fx_outside) {
            walker* fx_inside_p = fx_inside.get();
            walker* fx_outside_p = fx_outside.get();
            og::runtime::current_session->myscreen_->world().fxlist.push_back(std::move(fx_inside));
            og::runtime::current_session->myscreen_->world().fxlist.push_back(std::move(fx_outside));
            fx_inside_p->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            fx_outside_p->setxy(GRID_SIZE * 20, GRID_SIZE * 20);
            TEST_ASSERT(are_objects_outside_area(og::runtime::current_session->myscreen_->world(), 0, 0, 10, 10),
                        "outside fx object should be detected");
            fx_outside_p->setxy(GRID_SIZE * 3, GRID_SIZE * 3);
            TEST_ASSERT(!are_objects_outside_area(og::runtime::current_session->myscreen_->world(), 0, 0, 10, 10),
                        "inside fx objects should report false");
        }

        auto weap_inside = l ? l->create_walker_owned(Order::Weapon, FAMILY_BOMB) : nullptr;
        auto weap_outside = l ? l->create_walker_owned(Order::Weapon, FAMILY_BOMB) : nullptr;
        TEST_ASSERT(weap_inside != nullptr && weap_outside != nullptr, "weapon objects should be created");
        if (weap_inside && weap_outside) {
            walker* weap_inside_p = weap_inside.get();
            walker* weap_outside_p = weap_outside.get();
            og::runtime::current_session->myscreen_->world().weaplist.push_back(std::move(weap_inside));
            og::runtime::current_session->myscreen_->world().weaplist.push_back(std::move(weap_outside));
            weap_inside_p->setxy(GRID_SIZE * 2, GRID_SIZE * 2);
            weap_outside_p->setxy(GRID_SIZE * 20, GRID_SIZE * 20);
            TEST_ASSERT(are_objects_outside_area(og::runtime::current_session->myscreen_->world(), 0, 0, 10, 10),
                        "outside weapon object should be detected");
            weap_outside_p->setxy(GRID_SIZE * 3, GRID_SIZE * 3);
            TEST_ASSERT(!are_objects_outside_area(og::runtime::current_session->myscreen_->world(), 0, 0, 10, 10),
                        "inside weapon objects should report false");
        }
    }
    og::runtime::current_session->myscreen_->world().delete_objects();

    delete_campaign(id);
}
REGISTER_TEST(test_level_editor_create_new_campaign_and_detect_exists);
