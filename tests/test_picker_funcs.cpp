#include "graph.h"
#include "test_framework.h"
#include <cstring>
#include <string>

// Forward declarations from picker.cpp
std::string get_class_description(unsigned char family);
const char* family_name_copy(short family);
const char* get_family_string(short family);
const char* get_training_cost_rating(unsigned char family, int stat);
const char* get_random_name(unsigned char family);
bool has_name_in_team(const char* name);
const char* get_new_name(unsigned char family);
Sint32 how_many(Sint32 whatfamily);

extern screen* myscreen;

extern Sint32 costlist[NUM_FAMILIES];
extern Sint32 statlist[NUM_FAMILIES][6];
extern Sint32 statcosts[NUM_FAMILIES][6];

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
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
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

    TEST_ASSERT(std::string(family_name_copy(FAMILY_BIG_ORC)) == "ORC CAP.", "big orc short label");
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
            const char* rating = get_training_cost_rating(fam, stat);
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
    const char* name = get_random_name(FAMILY_SOLDIER);
    TEST_ASSERT(name != nullptr, "random name should not be null");
    TEST_ASSERT(strlen(name) > 0, "random name should not be empty");
}
REGISTER_TEST(test_get_random_name_returns_nonempty);

void test_get_random_name_all_families()
{
    srand(42);
    short families[] = { FAMILY_SOLDIER, FAMILY_ELF, FAMILY_ARCHER, FAMILY_MAGE,
                        FAMILY_SKELETON, FAMILY_CLERIC, FAMILY_FIREELEMENTAL,
                        FAMILY_FAERIE, FAMILY_SMALL_SLIME, FAMILY_THIEF,
                        FAMILY_GHOST, FAMILY_DRUID, FAMILY_ORC, FAMILY_BARBARIAN };
    for (int i = 0; i < 14; i++) {
        const char* name = get_random_name(families[i]);
        TEST_ASSERT(name != nullptr, "random name should not be null for any family");
        TEST_ASSERT(strlen(name) > 0, "random name should not be empty for any family");
    }

    TEST_ASSERT(get_new_name(FAMILY_SOLDIER) != nullptr, "new name should not be null");
    TEST_ASSERT(strlen(get_new_name(FAMILY_SOLDIER)) > 0, "new name should not be empty");
}
REGISTER_TEST(test_get_random_name_all_families);

// ---------------------------------------------------------------------------
// has_name_in_team tests
// ---------------------------------------------------------------------------

void test_has_name_in_team_empty()
{
    // Save and clear team
    int orig_size = myscreen->save_data.team_size;
    myscreen->save_data.team_size = 0;

    bool result = has_name_in_team("TestName");
    TEST_ASSERT(!result, "empty team should not have any names");

    guy* g = new guy(FAMILY_SOLDIER);
    g->name = "TestName";
    myscreen->save_data.team_list[0] = g;
    myscreen->save_data.team_size = 1;
    TEST_ASSERT(has_name_in_team("TestName"), "has_name_in_team should detect existing name");
    TEST_ASSERT(!has_name_in_team("OtherName"), "has_name_in_team should reject missing name");
    delete g;
    myscreen->save_data.team_list[0] = nullptr;

    myscreen->save_data.team_size = orig_size;
}
REGISTER_TEST(test_has_name_in_team_empty);

// ---------------------------------------------------------------------------
// how_many tests
// ---------------------------------------------------------------------------

void test_how_many_empty_team()
{
    int orig_size = myscreen->save_data.team_size;
    guy* orig_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        orig_list[i] = myscreen->save_data.team_list[i];
        myscreen->save_data.team_list[i] = nullptr;
    }

    Sint32 count = how_many(FAMILY_SOLDIER);
    TEST_ASSERT_EQ(0, (int)count, "empty team should have 0 of any family");

    // Restore
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        myscreen->save_data.team_list[i] = orig_list[i];
    }
    myscreen->save_data.team_size = orig_size;
}
REGISTER_TEST(test_how_many_empty_team);

void test_how_many_with_team()
{
    // Save originals
    int orig_size = myscreen->save_data.team_size;
    guy* orig_list[MAX_TEAM_SIZE];
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        orig_list[i] = myscreen->save_data.team_list[i];
        myscreen->save_data.team_list[i] = nullptr;
    }

    // Add some guys
    guy* g1 = new guy(FAMILY_SOLDIER);
    guy* g2 = new guy(FAMILY_SOLDIER);
    guy* g3 = new guy(FAMILY_MAGE);
    myscreen->save_data.team_list[0] = g1;
    myscreen->save_data.team_list[1] = g2;
    myscreen->save_data.team_list[2] = g3;
    myscreen->save_data.team_size = 3;

    TEST_ASSERT_EQ(2, (int)how_many(FAMILY_SOLDIER), "should count 2 soldiers");
    TEST_ASSERT_EQ(1, (int)how_many(FAMILY_MAGE), "should count 1 mage");
    TEST_ASSERT_EQ(0, (int)how_many(FAMILY_ARCHER), "should count 0 archers");

    // Cleanup
    delete g1; delete g2; delete g3;
    for (int i = 0; i < MAX_TEAM_SIZE; i++) {
        myscreen->save_data.team_list[i] = orig_list[i];
    }
    myscreen->save_data.team_size = orig_size;
}
REGISTER_TEST(test_how_many_with_team);
