#include "graph.h"
#include "io.h"
#include "test_framework.h"

extern screen* myscreen;

// ---------------------------------------------------------------------------
// explode() - string splitting utility
// ---------------------------------------------------------------------------

void test_io_explode_basic()
{
    auto result = explode("hello,world,foo", ',');
    TEST_ASSERT_EQ(3, (int)result.size(), "3 parts");
    auto it = result.begin();
    TEST_ASSERT(*it == "hello", "first part");
    ++it;
    TEST_ASSERT(*it == "world", "second part");
    ++it;
    TEST_ASSERT(*it == "foo", "third part");
}
REGISTER_TEST(test_io_explode_basic);

void test_io_explode_no_delimiter()
{
    auto result = explode("nodots", '.');
    TEST_ASSERT_EQ(1, (int)result.size(), "no delimiter = 1 part");
    TEST_ASSERT(result.front() == "nodots", "whole string");
}
REGISTER_TEST(test_io_explode_no_delimiter);

void test_io_explode_empty()
{
    auto result = explode("", ',');
    // Should return 1 empty string or empty list
    (void)result;
}
REGISTER_TEST(test_io_explode_empty);

void test_io_explode_trailing_delimiter()
{
    auto result = explode("a,b,", ',');
    TEST_ASSERT(result.size() >= 2, "at least 2 parts");
}
REGISTER_TEST(test_io_explode_trailing_delimiter);

// ---------------------------------------------------------------------------
// get_user_path / get_asset_path
// ---------------------------------------------------------------------------

void test_io_get_user_path()
{
    std::string path = get_user_path();
    TEST_ASSERT(!path.empty(), "user path not empty");
}
REGISTER_TEST(test_io_get_user_path);

void test_io_get_user_path_nonempty()
{
    std::string path = get_user_path();
    // Should have some characters
    TEST_ASSERT(path.size() > 1, "user path has content");
}
REGISTER_TEST(test_io_get_user_path_nonempty);

// ---------------------------------------------------------------------------
// list_files
// ---------------------------------------------------------------------------

void test_io_list_files()
{
    auto files = list_files("cfg/");
    // May or may not have files depending on mount state
    (void)files;
}
REGISTER_TEST(test_io_list_files);

// ---------------------------------------------------------------------------
// get_mounted_campaign
// ---------------------------------------------------------------------------

void test_io_get_mounted_campaign()
{
    std::string campaign = get_mounted_campaign();
    // May be empty or have a value
    (void)campaign;
}
REGISTER_TEST(test_io_get_mounted_campaign);

// ---------------------------------------------------------------------------
// list_campaigns
// ---------------------------------------------------------------------------

void test_io_list_campaigns()
{
    auto campaigns = list_campaigns();
    (void)campaigns;
}
REGISTER_TEST(test_io_list_campaigns);

// ---------------------------------------------------------------------------
// list_levels
// ---------------------------------------------------------------------------

void test_io_list_levels()
{
    auto levels = list_levels();
    (void)levels;
}
REGISTER_TEST(test_io_list_levels);

void test_io_list_levels_v()
{
    auto levels = list_levels_v();
    (void)levels;
}
REGISTER_TEST(test_io_list_levels_v);
