#include <gtest/gtest.h>

#include <openglad/core/sim_cadence.h>
#include <openglad/gameplay/net_constants.h>

#include <cmath>
#include <cstdint>
#include <type_traits>

namespace {

og::core::SimCadenceInputs make_default_inputs()
{
    return og::core::SimCadenceInputs{
        /*timer_wait=*/og::sim::DEFAULT_TIMER_WAIT,
        /*speed_factor=*/1.0f,
        /*fixed_tick_ms=*/0u,
        /*enable_frame_timing=*/true,
    };
}

TEST(SimCadence, master_default_returns_82ms)
{
    const og::core::SimCadenceResult result =
        og::core::compute_sim_interval_ms(make_default_inputs());

    const std::uint32_t expected = static_cast<std::uint32_t>(std::lround(
        og::sim::DEFAULT_TIMER_WAIT * og::sim::TIMER_WAIT_TO_MS));
    EXPECT_EQ(82u, expected); // sanity: 6 * 13.6 ≈ 82
    EXPECT_EQ(result.interval_ms, expected);
    EXPECT_FALSE(result.caller_manages_timing);
    EXPECT_FALSE(result.immediate_tick);
    EXPECT_STREQ(result.trace_event, "schedule_timer_wait_interval");
}

TEST(SimCadence, timer_wait_change_changes_interval)
{
    for (short tw : {short{1}, short{6}, short{10}, short{20}})
    {
        og::core::SimCadenceInputs inputs = make_default_inputs();
        inputs.timer_wait = tw;
        const og::core::SimCadenceResult result =
            og::core::compute_sim_interval_ms(inputs);

        const std::uint32_t expected = static_cast<std::uint32_t>(std::lround(
            tw * og::sim::TIMER_WAIT_TO_MS));
        EXPECT_EQ(result.interval_ms, expected) << "timer_wait=" << tw;
        EXPECT_STREQ(result.trace_event, "schedule_timer_wait_interval");
    }
}

TEST(SimCadence, zero_speed_factor_returns_one_ms)
{
    og::core::SimCadenceInputs inputs = make_default_inputs();
    inputs.speed_factor = 0.0f;

    const og::core::SimCadenceResult result =
        og::core::compute_sim_interval_ms(inputs);

    EXPECT_EQ(result.interval_ms, 1u);
    EXPECT_FALSE(result.caller_manages_timing);
    EXPECT_FALSE(result.immediate_tick);
    EXPECT_STREQ(result.trace_event, "schedule_timer_wait_interval");
}

TEST(SimCadence, fixed_tick_ms_overrides_default)
{
    for (short tw : {short{1}, short{6}, short{20}})
    {
        og::core::SimCadenceInputs inputs = make_default_inputs();
        inputs.timer_wait = tw;
        inputs.fixed_tick_ms = 50u;
        const og::core::SimCadenceResult result =
            og::core::compute_sim_interval_ms(inputs);

        EXPECT_EQ(result.interval_ms, 50u) << "timer_wait=" << tw;
        EXPECT_FALSE(result.caller_manages_timing);
        EXPECT_FALSE(result.immediate_tick);
        EXPECT_STREQ(result.trace_event, "schedule_fixed_interval");
    }
}

TEST(SimCadence, external_timing_returns_caller_managed)
{
    og::core::SimCadenceInputs inputs = make_default_inputs();
    inputs.enable_frame_timing = false;

    const og::core::SimCadenceResult result =
        og::core::compute_sim_interval_ms(inputs);

    EXPECT_EQ(result.interval_ms, 0u);
    EXPECT_TRUE(result.caller_manages_timing);
    EXPECT_TRUE(result.immediate_tick);
    EXPECT_STREQ(result.trace_event, "schedule_external_timing");
}

template <typename T, typename = void>
struct has_target_fps_member : std::false_type {};

template <typename T>
struct has_target_fps_member<T, std::void_t<decltype(std::declval<T&>().target_fps)>>
    : std::true_type {};

TEST(SimCadence, does_not_consult_target_fps)
{
    static_assert(!has_target_fps_member<og::core::SimCadenceInputs>::value,
                  "SimCadenceInputs must not expose a target_fps field");
}

} // namespace
