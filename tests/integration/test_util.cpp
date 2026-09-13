#include <chrono>
#include <cstdint>
#include <cstring>
#include <string>

#include <gtest/gtest.h>
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

    auto blank = parse_int_strict("   \t   ");
    ASSERT_TRUE(!blank.has_value()) << "parse_int_strict should reject whitespace-only text";
}


// A tick is 13.6 ms (the DOS 1193180/16383 Hz cadence util.cpp keeps), so
// time_delay(kDelayTicks) is ~68 ms of wall clock.
static constexpr std::int32_t kDelayTicks = 5;
static constexpr std::int32_t kDelayTicksFloor = 4;   // 68 ms / 13.6, minus rounding
static constexpr std::int32_t kSinceResetCeiling = 40; // ~544 ms: far under "since app start"

TEST(Util, timer_counts_from_the_last_reset_and_time_delay_waits)
{
    // change_time is a retained no-op stub (util.cpp) with no observable of
    // its own; this is its only caller, kept here with the timer family.
    change_time(12345);

    reset_timer();
    const std::int32_t control_before = query_timer_control();

    time_delay(kDelayTicks);

    const std::int32_t elapsed = query_timer();
    ASSERT_GE(elapsed, kDelayTicksFloor)
        << "time_delay(5) must actually wait ~68 ms and query_timer must report it as elapsed/13.6 ticks";
    ASSERT_LE(elapsed, kSinceResetCeiling)
        << "query_timer must measure from the last reset_timer, not from process start";

    ASSERT_GE(query_timer_control() - control_before, kDelayTicksFloor)
        << "query_timer_control must advance with wall clock (ms since app start / 13.6)";

    reset_timer();
    ASSERT_LT(query_timer(), elapsed)
        << "reset_timer must re-stamp the reference so the tick count restarts";
}


TEST(Util, time_delay_returns_immediately_for_non_positive_delays)
{
    const auto start = std::chrono::steady_clock::now();
    time_delay(0);
    time_delay(-1);
    const auto spent_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start).count();
    ASSERT_LT(spent_ms, 20)
        << "time_delay(<= 0) must return without waiting";
}

