#include <cstring>
#include <string>

#include "test_framework.h"
#include <openglad/core/util.h>

void test_util_case_conversion_cstr()
{
    char buf[32];
    std::strncpy(buf, "AbC123!?", sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    lowercase(buf);
    TEST_ASSERT_STR_EQ("abc123!?", buf, "lowercase(char*) should lowercase ASCII letters");

    uppercase(buf);
    TEST_ASSERT_STR_EQ("ABC123!?", buf, "uppercase(char*) should uppercase ASCII letters");
}
REGISTER_TEST(test_util_case_conversion_cstr);

void test_util_case_conversion_string()
{
    std::string s = "HeLLo-123";
    lowercase(s);
    TEST_ASSERT_STR_EQ("hello-123", s.c_str(), "lowercase(string) should lowercase ASCII letters");

    uppercase(s);
    TEST_ASSERT_STR_EQ("HELLO-123", s.c_str(), "uppercase(string) should uppercase ASCII letters");
}
REGISTER_TEST(test_util_case_conversion_string);

void test_util_game_speed_clamps()
{
    set_game_speed(-5.0f);
    TEST_ASSERT(g_game_speed_factor == 0.0f, "set_game_speed should clamp negatives to 0");

    set_game_speed(2.0f);
    TEST_ASSERT(g_game_speed_factor == 2.0f, "set_game_speed should accept >1 factors");
}
REGISTER_TEST(test_util_game_speed_clamps);

