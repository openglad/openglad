#include <openglad/gameplay/net_constants.h>
#include <openglad/server/headless_tick_interval.h>

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

TEST(HeadlessTickInterval, default_returns_82ms)
{
    EXPECT_EQ(82u,
              og::server::compute_headless_tick_interval_ms(
                  og::sim::DEFAULT_TIMER_WAIT));
}

TEST(HeadlessTickInterval, timer_wait_one_returns_at_least_one)
{
    const std::uint32_t expected =
        static_cast<std::uint32_t>(std::lround(1 * og::sim::TIMER_WAIT_TO_MS));
    EXPECT_EQ(14u, expected);
    EXPECT_EQ(expected, og::server::compute_headless_tick_interval_ms(1));
}

TEST(HeadlessTickInterval, timer_wait_twenty_returns_272ms)
{
    const std::uint32_t expected =
        static_cast<std::uint32_t>(std::lround(20 * og::sim::TIMER_WAIT_TO_MS));
    EXPECT_EQ(272u, expected);
    EXPECT_EQ(expected, og::server::compute_headless_tick_interval_ms(20));
}

TEST(HeadlessTickInterval, zero_or_negative_clamps_to_default)
{
    EXPECT_EQ(82u, og::server::compute_headless_tick_interval_ms(0));
    EXPECT_EQ(82u, og::server::compute_headless_tick_interval_ms(-5));
}
