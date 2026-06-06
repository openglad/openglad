#include <gtest/gtest.h>

#include <openglad/core/frame_pacing.h>
#include <openglad/core/runtime_trace.h>

#include <cstdint>

namespace {

class RuntimeTraceCaptureGuard
{
public:
    RuntimeTraceCaptureGuard()
        : was_enabled_(og::runtime::runtime_trace_enabled())
    {
        og::runtime::reset_runtime_trace_capture_state();
        og::runtime::set_runtime_trace_enabled(true);
    }

    ~RuntimeTraceCaptureGuard()
    {
        og::runtime::set_runtime_trace_enabled(was_enabled_);
        og::runtime::reset_runtime_trace_capture_state();
    }

    static int count_events(std::string_view category, std::string_view event)
    {
        int count = 0;
        for (const auto& record : og::runtime::copy_runtime_trace())
        {
            if (record.category == category && record.event == event)
                ++count;
        }
        return count;
    }

private:
    bool was_enabled_ = false;
};

TEST(FrameDeadlinePacer, SteadyStateAdvancesExactlyOneIntervalPerCall)
{
    constexpr std::uint32_t kInterval = 14u;
    constexpr int kFrames = 1000;

    og::core::FrameDeadlinePacer pacer;
    pacer.configure(kInterval, 0u);

    std::uint32_t now = kInterval;
    int ticks_executed = 0;
    std::uint32_t last_deadline = 0u;
    for (int i = 0; i < kFrames; ++i)
    {
        const auto decision = pacer.tick(now);
        ASSERT_TRUE(decision.run_tick) << "frame " << i;
        ASSERT_TRUE(decision.run_render) << "frame " << i;
        ASSERT_EQ(decision.sleep_ms, 0u);
        last_deadline = decision.next_deadline_ms;
        ++ticks_executed;
        now += kInterval;
    }

    EXPECT_EQ(ticks_executed, kFrames);
    EXPECT_EQ(last_deadline, kInterval + static_cast<std::uint32_t>(kFrames) * kInterval);
}

TEST(FrameDeadlinePacer, MidIntervalReportsRemainingSleep)
{
    og::core::FrameDeadlinePacer pacer;
    pacer.configure(14u, 0u);

    const auto d0 = pacer.tick(0u);
    EXPECT_FALSE(d0.run_tick);
    EXPECT_FALSE(d0.run_render);
    EXPECT_EQ(d0.sleep_ms, 14u);
    EXPECT_EQ(d0.next_deadline_ms, 14u);

    const auto d1 = pacer.tick(7u);
    EXPECT_FALSE(d1.run_tick);
    EXPECT_EQ(d1.sleep_ms, 7u);
    EXPECT_EQ(d1.next_deadline_ms, 14u);

    const auto d2 = pacer.tick(13u);
    EXPECT_FALSE(d2.run_tick);
    EXPECT_EQ(d2.sleep_ms, 1u);
}

TEST(FrameDeadlinePacer, NoDoubleRunTickWithinInterval)
{
    og::core::FrameDeadlinePacer pacer;
    pacer.configure(14u, 0u);

    // First call at the exact deadline runs.
    const auto first = pacer.tick(14u);
    ASSERT_TRUE(first.run_tick);

    // Subsequent calls before the next deadline (28) must not run a tick.
    for (std::uint32_t now = 15u; now < 28u; ++now)
    {
        const auto decision = pacer.tick(now);
        ASSERT_FALSE(decision.run_tick) << "now=" << now;
        ASSERT_FALSE(decision.run_render) << "now=" << now;
        ASSERT_EQ(decision.sleep_ms, 28u - now);
    }

    const auto next = pacer.tick(28u);
    EXPECT_TRUE(next.run_tick);
}

TEST(FrameDeadlinePacer, IntervalZeroClampsToOne)
{
    og::core::FrameDeadlinePacer pacer;
    pacer.configure(0u, 100u);
    EXPECT_EQ(pacer.interval_ms(), 1u);

    const auto d = pacer.tick(100u);
    EXPECT_FALSE(d.run_tick);
    EXPECT_EQ(d.sleep_ms, 1u);
    EXPECT_EQ(d.next_deadline_ms, 101u);

    const auto d2 = pacer.tick(101u);
    EXPECT_TRUE(d2.run_tick);
    EXPECT_EQ(d2.next_deadline_ms, 102u);
}

TEST(FrameDeadlinePacer, SmallSlipRunsOneTickWithoutResync)
{
    RuntimeTraceCaptureGuard guard;

    og::core::FrameDeadlinePacer pacer;
    pacer.configure(14u, 0u);

    // Slip by exactly one interval (within the slip tolerance).
    const auto decision = pacer.tick(28u);
    EXPECT_TRUE(decision.run_tick);
    EXPECT_TRUE(decision.run_render);
    EXPECT_EQ(decision.sleep_ms, 0u);
    EXPECT_EQ(decision.next_deadline_ms, 28u);

    EXPECT_EQ(RuntimeTraceCaptureGuard::count_events("frame_pacing", "pacer_resync"), 0);
}

TEST(FrameDeadlinePacer, LargeSlipEmitsResyncAndRunsExactlyOneTick)
{
    RuntimeTraceCaptureGuard guard;

    og::core::FrameDeadlinePacer pacer;
    pacer.configure(14u, 0u);

    // Run a normal frame to advance the deadline.
    const auto first = pacer.tick(14u);
    ASSERT_TRUE(first.run_tick);
    ASSERT_EQ(first.next_deadline_ms, 28u);

    // Now stall: time jumps far beyond the next deadline.
    const std::uint32_t now = 1000u;
    const auto resync = pacer.tick(now);
    EXPECT_TRUE(resync.run_tick) << "resync frame must still run exactly one tick";
    EXPECT_TRUE(resync.run_render);
    EXPECT_EQ(resync.sleep_ms, 0u);
    EXPECT_EQ(resync.next_deadline_ms, now + 14u);

    EXPECT_EQ(RuntimeTraceCaptureGuard::count_events("frame_pacing", "pacer_resync"), 1)
        << "exactly one pacer_resync trace expected";

    // The next call should NOT immediately run a second catch-up tick.
    const auto follow = pacer.tick(now);
    EXPECT_FALSE(follow.run_tick) << "no burst catch-up after resync";
    EXPECT_EQ(follow.sleep_ms, 14u);
}

TEST(FrameDeadlinePacer, UninitializedTickInitializesAndEmitsResync)
{
    RuntimeTraceCaptureGuard guard;

    og::core::FrameDeadlinePacer pacer;
    const auto decision = pacer.tick(500u);
    EXPECT_FALSE(decision.run_tick);
    EXPECT_FALSE(decision.run_render);
    EXPECT_EQ(decision.sleep_ms, 1u);
    EXPECT_EQ(decision.next_deadline_ms, 501u);
    EXPECT_EQ(pacer.interval_ms(), 1u);

    EXPECT_EQ(RuntimeTraceCaptureGuard::count_events("frame_pacing", "pacer_resync"), 1);
}

TEST(FrameDeadlinePacer, ConfigureThenTickDeterministicWithInjectedClock)
{
    og::core::FrameDeadlinePacer pacer_a;
    og::core::FrameDeadlinePacer pacer_b;
    pacer_a.configure(17u, 5u);
    pacer_b.configure(17u, 5u);

    const std::uint32_t timeline[] = {5u, 10u, 22u, 30u, 39u, 50u, 56u, 73u};
    for (std::uint32_t now : timeline)
    {
        const auto a = pacer_a.tick(now);
        const auto b = pacer_b.tick(now);
        EXPECT_EQ(a.run_tick, b.run_tick) << "now=" << now;
        EXPECT_EQ(a.run_render, b.run_render) << "now=" << now;
        EXPECT_EQ(a.sleep_ms, b.sleep_ms) << "now=" << now;
        EXPECT_EQ(a.next_deadline_ms, b.next_deadline_ms) << "now=" << now;
    }
}

TEST(FrameDeadlinePacer, ResetReanchorsDeadline)
{
    og::core::FrameDeadlinePacer pacer;
    pacer.configure(14u, 0u);
    (void)pacer.tick(14u);

    pacer.reset(100u);
    const auto d = pacer.tick(100u);
    EXPECT_FALSE(d.run_tick);
    EXPECT_EQ(d.sleep_ms, 14u);
    EXPECT_EQ(d.next_deadline_ms, 114u);
}

} // namespace
