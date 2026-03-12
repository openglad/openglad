#include <cstring>
#include <string>

#include "test_framework.h"
#include <openglad/core/util.h>
#include <openglad/legacy/base.h>  // g_game_speed_factor macro, set_game_speed()

TEST(Util, case_conversion_cstr)
{
    char buf[32];
    std::strncpy(buf, "AbC123!?", sizeof(buf));
    buf[sizeof(buf) - 1] = '\0';

    lowercase(buf);
    ASSERT_STREQ("abc123!?", buf) << "lowercase(char*) should lowercase ASCII letters";

    uppercase(buf);
    ASSERT_STREQ("ABC123!?", buf) << "uppercase(char*) should uppercase ASCII letters";
}


TEST(Util, case_conversion_string)
{
    std::string s = "HeLLo-123";
    lowercase(s);
    ASSERT_STREQ("hello-123", s.c_str()) << "lowercase(string) should lowercase ASCII letters";

    uppercase(s);
    ASSERT_STREQ("HELLO-123", s.c_str()) << "uppercase(string) should uppercase ASCII letters";
}


TEST(Util, game_speed_clamps)
{
    set_game_speed(-5.0f);
    ASSERT_TRUE(og::runtime::current_session->g_game_speed_factor_ == 0.0f) << "set_game_speed should clamp negatives to 0";

    set_game_speed(2.0f);
    ASSERT_TRUE(og::runtime::current_session->g_game_speed_factor_ == 2.0f) << "set_game_speed should accept >1 factors";
}


TEST(Util, parse_int_prefix_paths)
{
    auto v1 = parse_int_prefix("   -42xyz");
    ASSERT_TRUE(v1.has_value()) << "parse_int_prefix should parse leading integer with trailing text";
    ASSERT_EQ(-42, *v1) << "parse_int_prefix should parse signed values";

    auto v2 = parse_int_prefix("xyz");
    ASSERT_TRUE(!v2.has_value()) << "parse_int_prefix should fail when no prefix integer exists";

    auto v3 = parse_int_prefix("   ");
    ASSERT_TRUE(!v3.has_value()) << "parse_int_prefix should fail for whitespace-only input";
}


TEST(Util, parse_int_strict_paths)
{
    auto ok = parse_int_strict("  123  ");
    ASSERT_TRUE(ok.has_value()) << "parse_int_strict should allow surrounding whitespace";
    ASSERT_EQ(123, *ok) << "parse_int_strict should parse clean integer text";

    auto trailing = parse_int_strict("77abc");
    ASSERT_TRUE(!trailing.has_value()) << "parse_int_strict should reject trailing non-whitespace";

    auto overflow = parse_int_strict("999999999999999999999");
    ASSERT_TRUE(!overflow.has_value()) << "parse_int_strict should reject out-of-range numbers";
}


TEST(Util, timers_and_delay_paths)
{
    reset_timer();
    ASSERT_TRUE(query_timer() >= 0) << "query_timer should return non-negative tick count";
    ASSERT_TRUE(query_timer_control() >= 0) << "query_timer_control should return non-negative tick count";

    // Cover early-return and non-negative branches without introducing wall-clock delay.
    time_delay(-1);
    time_delay(0);
}


TEST(Util, change_time_and_strict_empty_after_trim)
{
    change_time(12345);

    auto empty = parse_int_strict("   \t   ");
    ASSERT_TRUE(!empty.has_value()) << "parse_int_strict should reject whitespace-only text";
}

