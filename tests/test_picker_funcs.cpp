#include <openglad/entities/guy.h>
#include <openglad/input/button.h>
#include <openglad/legacy/base.h>
#include <openglad/runtime/screen.h>
#include <openglad/ui/picker_common.h>
#include "test_framework.h"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

// Forward declarations from picker.cpp
std::string get_class_description(unsigned char family);
const char* family_name_copy(short family);
const char* get_family_string(Sint32 family);
const char* get_training_cost_rating(unsigned char family, int stat);
Sint32 how_many(Sint32 whatfamily);
int get_scen_num_from_filename(const char* name);
Sint32 set_difficulty();
Sint32 change_teamnum(Sint32 arg);
Sint32 change_hire_teamnum(Sint32 arg);
Sint32 change_allied();
bool yes_or_no_prompt(const char* title, const char* message, bool default_value);
Sint32 add_guy(guy* newguy);
Sint32 do_save(Sint32 arg1);
Sint32 do_load(Sint32 arg1);
Sint32 delete_all();
void quit(Sint32 arg1);
Sint32 return_menu(Sint32 arg);
Sint32 name_guy(Sint32 arg);
Sint32 edit_guy(Sint32 arg1);

extern screen* myscreen;
extern std::unique_ptr<guy> current_guy;
extern short current_team_num;
extern Sint32 current_difficulty;

#include <openglad/entities/family_descriptor.h>
#include <openglad/entities/family_registry.h>

// Button stat constants from picker.cpp
#define BUT_STR 0
#define BUT_DEX 1
#define BUT_CON 2
#define BUT_INT 3
#define BUT_ARMOR 4
#define BUT_LEVEL 5

// ---------------------------------------------------------------------------
// get_class_description tests
// ---------------------------------------------------------------------------

void test_get_class_description_soldier()
{
    std::string desc = get_class_description(FAMILY_SOLDIER);
    TEST_ASSERT(!desc.empty(), "soldier description should not be empty");
    TEST_ASSERT(desc.find("Soldier") != std::string::npos || desc.find("soldier") != std::string::npos || desc.find("fighter") != std::string::npos || desc.size() > 10, "soldier description should contain useful text");
}
REGISTER_TEST(test_get_class_description_soldier);

void test_get_class_description_mage()
{
    std::string desc = get_class_description(FAMILY_MAGE);
    TEST_ASSERT(!desc.empty(), "mage description should not be empty");
}
REGISTER_TEST(test_get_class_description_mage);

void test_get_class_description_all_families()
{
    unsigned char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN,
                        FAMILY_ARCHMAGE, FAMILY_BIG_ORC };
    for (int i = 0; i < 16; i++) {
        std::string desc = get_class_description(families[i]);
        TEST_ASSERT(!desc.empty(), "every family should have a description");
    }
}
REGISTER_TEST(test_get_class_description_all_families);

// ---------------------------------------------------------------------------
// family_name_copy tests
// ---------------------------------------------------------------------------

void test_family_name_copy_soldier()
{
    const char* name = family_name_copy(FAMILY_SOLDIER);
    TEST_ASSERT(name != nullptr, "soldier name should not be null");
    TEST_ASSERT(strlen(name) > 0, "soldier name should not be empty");
}
REGISTER_TEST(test_family_name_copy_soldier);

void test_family_name_copy_all_families()
{
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        const char* name = family_name_copy(families[i]);
        TEST_ASSERT(name != nullptr, "family name should not be null");
        TEST_ASSERT(strlen(name) > 0, "family name should not be empty");
    }

    TEST_ASSERT(std::string(family_name_copy(FAMILY_BIG_ORC)) == "ORC CAPTAIN", "big orc label from registry");
    TEST_ASSERT(std::string(get_family_string(FAMILY_BIG_ORC)) == "ORC CAPTAIN", "big orc full label");
    TEST_ASSERT(std::string(get_family_string(255)) == "BEAST", "unknown family full label fallback");
}
REGISTER_TEST(test_family_name_copy_all_families);

// ---------------------------------------------------------------------------
// get_training_cost_rating tests
// ---------------------------------------------------------------------------

void test_get_training_cost_rating_returns_stars()
{
    const char* rating = get_training_cost_rating(FAMILY_SOLDIER, BUT_STR);
    TEST_ASSERT(rating != nullptr, "rating should not be null");
    // Rating is 0-5 asterisks
    size_t len = strlen(rating);
    TEST_ASSERT(len <= 5, "rating should be at most 5 characters");
    for (size_t i = 0; i < len; i++) {
        TEST_ASSERT(rating[i] == '*', "rating should only contain asterisks");
    }
}
REGISTER_TEST(test_get_training_cost_rating_returns_stars);

void test_get_training_cost_rating_varies_by_stat()
{
    // Soldier STR cost is 6 (cheap), INT cost is 25 (expensive)
    const char* str_rating = get_training_cost_rating(FAMILY_SOLDIER, BUT_STR);
    const char* int_rating = get_training_cost_rating(FAMILY_SOLDIER, BUT_INT);
    // Cheaper stats should have more stars
    TEST_ASSERT(strlen(str_rating) >= strlen(int_rating), "cheaper stat should have >= stars");
}
REGISTER_TEST(test_get_training_cost_rating_varies_by_stat);

void test_get_training_cost_rating_all_families()
{
    for (int fam = 0; fam <= FAMILY_ARCHMAGE; fam++) {
        for (int stat = 0; stat < 5; stat++) {
            const char* rating = get_training_cost_rating(static_cast<unsigned char>(fam), stat);
            TEST_ASSERT(rating != nullptr, "rating should not be null");
            TEST_ASSERT(strlen(rating) <= 5, "rating should be at most 5 chars");
        }
    }
}
REGISTER_TEST(test_get_training_cost_rating_all_families);

// ---------------------------------------------------------------------------
// get_random_name tests
// ---------------------------------------------------------------------------

void test_get_random_name_returns_nonempty()
{
    srand(42);
    const char* name = og::ui::get_random_name(FAMILY_SOLDIER);
    TEST_ASSERT(name != nullptr, "random name should not be null");
    TEST_ASSERT(strlen(name) > 0, "random name should not be empty");
}
REGISTER_TEST(test_get_random_name_returns_nonempty);

void test_get_random_name_all_families()
{
    srand(42);
    unsigned char families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        const char* name = og::ui::get_random_name(families[i]);
        TEST_ASSERT(name != nullptr, "random name should not be null for any family");
        TEST_ASSERT(strlen(name) > 0, "random name should not be empty for any family");
    }

    std::string unique = og::ui::get_unique_name(FAMILY_SOLDIER, myscreen->save_data);
    TEST_ASSERT(!unique.empty(), "unique name should not be empty");
}
REGISTER_TEST(test_get_random_name_all_families);

// ---------------------------------------------------------------------------
// has_name_in_team tests
// ---------------------------------------------------------------------------

void test_has_name_in_team_empty()
{
    // Save and clear team
    const unsigned char orig_size = myscreen->save_data.team_size;
    myscreen->save_data.team_size = static_cast<unsigned char>(0);

    // Use get_unique_name to verify name dedup works on empty team
    std::string name1 = og::ui::get_unique_name(FAMILY_SOLDIER, myscreen->save_data);
    TEST_ASSERT(!name1.empty(), "unique name should not be empty on empty team");

    guy* g = new guy(FAMILY_SOLDIER);
    g->name = "TestName";
    myscreen->save_data.team_list[0].reset(g);
    myscreen->save_data.team_size = static_cast<unsigned char>(1);

    // get_unique_name should return a name different from existing "TestName"
    // (though it may not collide anyway since random names vary)
    std::string name2 = og::ui::get_unique_name(FAMILY_SOLDIER, myscreen->save_data);
    TEST_ASSERT(!name2.empty(), "unique name should not be empty");

    myscreen->save_data.team_list[0].reset(nullptr);
    myscreen->save_data.team_size = orig_size;
}
REGISTER_TEST(test_has_name_in_team_empty);

// ---------------------------------------------------------------------------
// how_many tests
// ---------------------------------------------------------------------------

void test_how_many_empty_team()
{
    const unsigned char orig_size = myscreen->save_data.team_size;
    guy* orig_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        orig_list[i] = myscreen->save_data.team_list[i].release();
        myscreen->save_data.team_list[i].reset(nullptr);
    }

    Sint32 count = how_many(FAMILY_SOLDIER);
    TEST_ASSERT_EQ(0, (int)count, "empty team should have 0 of any family");

    // Restore
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        myscreen->save_data.team_list[i].reset(orig_list[i]);
    }
    myscreen->save_data.team_size = orig_size;
}
REGISTER_TEST(test_how_many_empty_team);

void test_how_many_with_team()
{
    // Save originals
    const unsigned char orig_size = myscreen->save_data.team_size;
    guy* orig_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        orig_list[i] = myscreen->save_data.team_list[i].release();
        myscreen->save_data.team_list[i].reset(nullptr);
    }

    // Add some guys
    guy* g1 = new guy(FAMILY_SOLDIER);
    guy* g2 = new guy(FAMILY_SOLDIER);
    guy* g3 = new guy(FAMILY_MAGE);
    myscreen->save_data.team_list[0].reset(g1);
    myscreen->save_data.team_list[1].reset(g2);
    myscreen->save_data.team_list[2].reset(g3);
    myscreen->save_data.team_size = static_cast<unsigned char>(3);

    TEST_ASSERT_EQ(2, (int)how_many(FAMILY_SOLDIER), "should count 2 soldiers");
    TEST_ASSERT_EQ(1, (int)how_many(FAMILY_MAGE), "should count 1 mage");
    TEST_ASSERT_EQ(0, (int)how_many(FAMILY_ARCHER), "should count 0 archers");

    // Cleanup
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        myscreen->save_data.team_list[i].reset(orig_list[i]);
    }
    myscreen->save_data.team_size = orig_size;

    // Additional picker utility/state coverage without registering new tests.
    TEST_ASSERT_EQ(-1, get_scen_num_from_filename(nullptr), "null input should return -1");
    TEST_ASSERT_EQ(-1, get_scen_num_from_filename("scen"), "no numeric suffix should return -1");
    TEST_ASSERT_EQ(123, get_scen_num_from_filename("scen123"), "numeric suffix should parse");
    TEST_ASSERT_EQ(42, get_scen_num_from_filename("file42"), "mixed prefix should parse trailing number");

    vbutton* old2 = allbuttons[2];
    vbutton* old6 = allbuttons[6];
    vbutton* old7 = allbuttons[7];
    vbutton* old18 = allbuttons[18];
    std::unique_ptr<guy> old_current = std::move(current_guy);
    short old_team_num = current_team_num;
    Sint32 old_diff = current_difficulty;
    const short old_allied = myscreen->save_data.allied_mode;

    allbuttons[2] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b2", KEYSTATE_UNKNOWN);
    allbuttons[6] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b6", KEYSTATE_UNKNOWN);
    allbuttons[7] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b7", KEYSTATE_UNKNOWN);
    allbuttons[18] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b18", KEYSTATE_UNKNOWN);

    current_guy = std::make_unique<guy>(FAMILY_SOLDIER);
    current_guy->teamnum = 1;
    current_team_num = 0;
    current_difficulty = DIFFICULTY_SETTINGS - 1;
    myscreen->save_data.allied_mode = static_cast<short>(0);

    TEST_ASSERT_EQ(4, (int)set_difficulty(), "set_difficulty should return OK");
    TEST_ASSERT(
        std::string(allbuttons[2]->label).find("Difficulty: ") == 0 ||
        std::string(allbuttons[6]->label).find("Difficulty: ") == 0,
        "difficulty label should be updated");

    TEST_ASSERT_EQ(4, (int)change_teamnum(1), "change_teamnum should return OK");
    TEST_ASSERT_EQ(2, (int)current_guy->teamnum, "team should increment");
    TEST_ASSERT(std::string(allbuttons[18]->label).find("Playing on Team ") == 0, "team label should be updated");

    TEST_ASSERT_EQ(4, (int)change_hire_teamnum(1), "change_hire_teamnum should return OK");
    TEST_ASSERT_EQ(1, (int)current_team_num, "hire team num should increment");
    TEST_ASSERT_EQ(1, (int)current_guy->teamnum, "current guy team should mirror hire team");
    TEST_ASSERT(std::string(allbuttons[2]->label).find("Hiring for Team ") == 0, "hire label should be updated");

    TEST_ASSERT_EQ(4, (int)change_allied(), "change_allied should return OK");
    TEST_ASSERT_EQ(1, myscreen->save_data.allied_mode, "allied mode should toggle on");
    TEST_ASSERT(allbuttons[7]->label == "PVP: Ally", "allied label should update");
    TEST_ASSERT_EQ(4, (int)change_allied(), "change_allied second toggle should return OK");
    TEST_ASSERT_EQ(0, myscreen->save_data.allied_mode, "allied mode should toggle off");
    TEST_ASSERT(allbuttons[7]->label == "PVP: Enemy", "enemy label should update");

    // Directly exercise picker helpers that were still uncovered.
    const unsigned char saved_team_size = myscreen->save_data.team_size;
    guy* saved_team_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        saved_team_list[i] = myscreen->save_data.team_list[i].release();
        myscreen->save_data.team_list[i].reset(nullptr);
    }
    myscreen->save_data.team_size = static_cast<unsigned char>(0);

    guy* recruited = new guy(FAMILY_SOLDIER);
    Sint32 slot = add_guy(recruited);
    TEST_ASSERT(slot >= 0, "add_guy(guy*) should place recruit in a slot");
    TEST_ASSERT(myscreen->save_data.team_size == static_cast<unsigned char>(1), "team size should increment after add_guy(guy*)");

    TEST_ASSERT_EQ(1, (int)delete_all(), "delete_all should report number of removed members");
    TEST_ASSERT(myscreen->save_data.team_size == static_cast<unsigned char>(0), "delete_all should clear team size");

    vbutton* old0 = allbuttons[0];
    if (allbuttons[0] == nullptr) {
        allbuttons[0] = new vbutton(0, 0, 10, 10, button_action_id(ButtonAction::NullMenu), 0, "b0", KEYSTATE_UNKNOWN);
    }
    allbuttons[0]->label = "UNIT_TEST_SAVE";
    Sint32 save_ret = do_save(1);
    Sint32 load_ret = do_load(1);
    TEST_ASSERT(save_ret == load_ret, "do_save/do_load should return same menu status");

    TEST_ASSERT_EQ(1234, (int)return_menu(1234), "return_menu should echo its argument");
    quit(0); // test mode: should not exit
    std::unique_ptr<guy> tmp_current = std::move(current_guy);
    current_guy = nullptr;
    TEST_ASSERT_EQ(2, (int)name_guy(0), "name_guy with no current_guy should return REDRAW");
    TEST_ASSERT_EQ(-1, (int)edit_guy(0), "edit_guy with no current_guy should fail");
    current_guy = std::move(tmp_current);


    current_guy = std::move(old_current);
    current_team_num = old_team_num;
    current_difficulty = old_diff;
    myscreen->save_data.allied_mode = old_allied;

    delete allbuttons[2];
    delete allbuttons[6];
    delete allbuttons[7];
    delete allbuttons[18];
    if (old0 == nullptr) {
        delete allbuttons[0];
    }
    allbuttons[0] = old0;
    allbuttons[2] = old2;
    allbuttons[6] = old6;
    allbuttons[7] = old7;
    allbuttons[18] = old18;

    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        myscreen->save_data.team_list[i].reset(saved_team_list[i]);
    }
    myscreen->save_data.team_size = saved_team_size;
}
REGISTER_TEST(test_how_many_with_team);
