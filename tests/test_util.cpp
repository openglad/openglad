#include <cstring>
#include <string>

#include "test_framework.h"
#include <openglad/core/util.h>
#include <openglad/legacy/base.h>  // g_game_speed_factor macro, set_game_speed()

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
    TEST_ASSERT(og::runtime::current_session->g_game_speed_factor_ == 0.0f, "set_game_speed should clamp negatives to 0");

    set_game_speed(2.0f);
    TEST_ASSERT(og::runtime::current_session->g_game_speed_factor_ == 2.0f, "set_game_speed should accept >1 factors");
}
REGISTER_TEST(test_util_game_speed_clamps);

void test_util_parse_int_prefix_paths()
{
    auto v1 = parse_int_prefix("   -42xyz");
    TEST_ASSERT(v1.has_value(), "parse_int_prefix should parse leading integer with trailing text");
    TEST_ASSERT_EQ(-42, *v1, "parse_int_prefix should parse signed values");

    auto v2 = parse_int_prefix("xyz");
    TEST_ASSERT(!v2.has_value(), "parse_int_prefix should fail when no prefix integer exists");

    auto v3 = parse_int_prefix("   ");
    TEST_ASSERT(!v3.has_value(), "parse_int_prefix should fail for whitespace-only input");
}
REGISTER_TEST(test_util_parse_int_prefix_paths);

void test_util_parse_int_strict_paths()
{
    auto ok = parse_int_strict("  123  ");
    TEST_ASSERT(ok.has_value(), "parse_int_strict should allow surrounding whitespace");
    TEST_ASSERT_EQ(123, *ok, "parse_int_strict should parse clean integer text");

    auto trailing = parse_int_strict("77abc");
    TEST_ASSERT(!trailing.has_value(), "parse_int_strict should reject trailing non-whitespace");

    auto overflow = parse_int_strict("999999999999999999999");
    TEST_ASSERT(!overflow.has_value(), "parse_int_strict should reject out-of-range numbers");
}
REGISTER_TEST(test_util_parse_int_strict_paths);

void test_util_timers_and_delay_paths()
{
    reset_timer();
    TEST_ASSERT(query_timer() >= 0, "query_timer should return non-negative tick count");
    TEST_ASSERT(query_timer_control() >= 0, "query_timer_control should return non-negative tick count");

    // Cover early-return and non-negative branches without introducing wall-clock delay.
    time_delay(-1);
    time_delay(0);
}
REGISTER_TEST(test_util_timers_and_delay_paths);

void test_util_change_time_and_strict_empty_after_trim()
{
    change_time(12345);

    auto empty = parse_int_strict("   \t   ");
    TEST_ASSERT(!empty.has_value(), "parse_int_strict should reject whitespace-only text");
}
REGISTER_TEST(test_util_change_time_and_strict_empty_after_trim);
