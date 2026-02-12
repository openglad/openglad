#include "graph.h"
#include "data/gparser.h"
#include "test_framework.h"

extern cfg_store cfg;

// ---------------------------------------------------------------------------
// cfg_store::apply_setting / get_setting
// ---------------------------------------------------------------------------

void test_gparser_apply_get_setting()
{
    cfg.apply_setting("test_cat", "test_key", "test_val");
    std::string result = cfg.get_setting("test_cat", "test_key");
    TEST_ASSERT(result == "test_val", "get_setting returns applied value");
}
REGISTER_TEST(test_gparser_apply_get_setting);

void test_gparser_get_setting_missing()
{
    std::string result = cfg.get_setting("nonexistent_cat", "nonexistent_key");
    TEST_ASSERT(result.empty(), "missing setting returns empty");
}
REGISTER_TEST(test_gparser_get_setting_missing);

void test_gparser_is_on_true()
{
    cfg.apply_setting("test_cat2", "enabled", "on");
    TEST_ASSERT(cfg.is_on("test_cat2", "enabled"), "on setting returns true");
}
REGISTER_TEST(test_gparser_is_on_true);

void test_gparser_is_on_false()
{
    cfg.apply_setting("test_cat2", "disabled", "off");
    TEST_ASSERT(!cfg.is_on("test_cat2", "disabled"), "off setting returns false");
}
REGISTER_TEST(test_gparser_is_on_false);

void test_gparser_is_on_missing()
{
    TEST_ASSERT(!cfg.is_on("missing_cat", "missing_key"), "missing setting returns false");
}
REGISTER_TEST(test_gparser_is_on_missing);

void test_gparser_overwrite_setting()
{
    cfg.apply_setting("test_cat3", "key1", "first");
    cfg.apply_setting("test_cat3", "key1", "second");
    std::string result = cfg.get_setting("test_cat3", "key1");
    TEST_ASSERT(result == "second", "overwritten setting has new value");
}
REGISTER_TEST(test_gparser_overwrite_setting);

void test_gparser_multiple_categories()
{
    cfg.apply_setting("catA", "key1", "valA");
    cfg.apply_setting("catB", "key1", "valB");
    TEST_ASSERT(cfg.get_setting("catA", "key1") == "valA", "catA value");
    TEST_ASSERT(cfg.get_setting("catB", "key1") == "valB", "catB value");
}
REGISTER_TEST(test_gparser_multiple_categories);

// ---------------------------------------------------------------------------
// cfg_store data direct access
// ---------------------------------------------------------------------------

void test_gparser_data_access()
{
    cfg.apply_setting("direct_cat", "direct_key", "direct_val");
    TEST_ASSERT(cfg.data.count("direct_cat") > 0, "category exists in data");
    TEST_ASSERT(cfg.data["direct_cat"]["direct_key"] == "direct_val", "value matches");
}
REGISTER_TEST(test_gparser_data_access);
