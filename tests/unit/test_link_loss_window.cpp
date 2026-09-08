// #278: og::sim::LinkLossWindow is the ONE rule for "a link that carried a
// session is lost" and "is dead for good" — GameClient's 30 s backstop and the
// lobby-parked joiner's revert both read it, so its timeline is pinned here.
#include <openglad/gameplay/link_loss_window.h>
#include <openglad/gameplay/net_constants.h>

#include <gtest/gtest.h>

#include <chrono>

using namespace std::chrono_literals;

namespace {

using Clock = og::sim::LinkLossWindow::Clock;

struct WindowOverrideGuard
{
    explicit WindowOverrideGuard(std::uint64_t ms)
    {
        og::sim::g_test_link_loss_window_ms_override = ms;
    }
    ~WindowOverrideGuard()
    {
        og::sim::g_test_link_loss_window_ms_override = 0;
    }
};

} // namespace

TEST(LinkLossWindow, never_connected_is_neither_lost_nor_expired)
{
    og::sim::LinkLossWindow window;
    const Clock::time_point t0 = Clock::now();
    EXPECT_FALSE(window.connected());
    EXPECT_FALSE(window.ever_connected());
    EXPECT_FALSE(window.lost());
    EXPECT_FALSE(window.expired(t0 + 1h))
        << "a first dial that never connected has no session to lose";

    window.observe(false, t0);
    EXPECT_FALSE(window.lost());
    EXPECT_FALSE(window.lost_since().has_value());
    EXPECT_FALSE(window.expired(t0 + 1h));
}

TEST(LinkLossWindow, loss_is_stamped_on_the_first_down_poll_and_expires_after_the_window)
{
    og::sim::LinkLossWindow window;
    const Clock::time_point t0 = Clock::now();
    window.observe(true, t0);
    EXPECT_TRUE(window.connected());
    EXPECT_TRUE(window.ever_connected());
    EXPECT_FALSE(window.lost());

    const Clock::time_point drop = t0 + 5s;
    window.observe(false, drop);
    EXPECT_TRUE(window.lost());
    ASSERT_TRUE(window.lost_since().has_value());
    EXPECT_EQ(drop, *window.lost_since());

    // Later down polls keep the FIRST stamp: the window runs from the drop.
    window.observe(false, drop + 3s);
    EXPECT_EQ(drop, *window.lost_since());

    const auto window_length =
        std::chrono::milliseconds(og::sim::CLIENT_CONNECTION_LOST_TIMEOUT_MS);
    EXPECT_FALSE(window.expired(drop + window_length - 1ms))
        << "inside the reconnect window the session is not over";
    EXPECT_TRUE(window.expired(drop + window_length));
    EXPECT_EQ(og::sim::CLIENT_CONNECTION_LOST_TIMEOUT_MS,
              og::sim::LinkLossWindow::window_ms());
}

TEST(LinkLossWindow, a_reconnect_inside_the_window_reopens_the_timeline)
{
    og::sim::LinkLossWindow window;
    const Clock::time_point t0 = Clock::now();
    window.observe(true, t0);
    window.observe(false, t0 + 1s);
    EXPECT_TRUE(window.lost());

    // The blip heals: not lost, no stamp, nothing expires.
    window.observe(true, t0 + 4s);
    EXPECT_TRUE(window.connected());
    EXPECT_FALSE(window.lost());
    EXPECT_FALSE(window.lost_since().has_value());
    EXPECT_FALSE(window.expired(t0 + 1h));

    // The NEXT loss starts a fresh window from its own drop, not the first.
    const Clock::time_point second_drop = t0 + 10s;
    window.observe(false, second_drop);
    ASSERT_TRUE(window.lost_since().has_value());
    EXPECT_EQ(second_drop, *window.lost_since());
    const auto window_length =
        std::chrono::milliseconds(og::sim::CLIENT_CONNECTION_LOST_TIMEOUT_MS);
    EXPECT_FALSE(window.expired(second_drop + window_length - 1ms));
    EXPECT_TRUE(window.expired(second_drop + window_length));
}

TEST(LinkLossWindow, reset_forgets_the_session_and_backdate_ages_a_loss)
{
    og::sim::LinkLossWindow window;
    const Clock::time_point t0 = Clock::now();
    window.observe(true, t0);
    window.observe(false, t0 + 1s);
    EXPECT_TRUE(window.lost());

    window.reset();
    EXPECT_FALSE(window.ever_connected());
    EXPECT_FALSE(window.lost());
    EXPECT_FALSE(window.lost_since().has_value());
    EXPECT_FALSE(window.expired(t0 + 1h)) << "a fresh dial is a fresh timeline";

    // backdate_loss: the link went down `elapsed` before `now`, but a link
    // that is UP is still not lost (the connected flag is left alone).
    og::sim::LinkLossWindow up;
    up.observe(true, t0);
    up.backdate_loss(
        static_cast<float>(og::sim::CLIENT_CONNECTION_LOST_TIMEOUT_MS + 1u), t0);
    EXPECT_TRUE(up.connected());
    EXPECT_FALSE(up.lost());
    EXPECT_FALSE(up.expired(t0));

    og::sim::LinkLossWindow down;
    down.backdate_loss(
        static_cast<float>(og::sim::CLIENT_CONNECTION_LOST_TIMEOUT_MS + 1u), t0);
    EXPECT_TRUE(down.ever_connected());
    EXPECT_TRUE(down.lost());
    EXPECT_TRUE(down.expired(t0));
    // A negative elapsed clamps to "just now".
    down.backdate_loss(-5.0f, t0);
    EXPECT_EQ(t0, *down.lost_since());
    EXPECT_FALSE(down.expired(t0));
}

TEST(LinkLossWindow, testing_override_shortens_the_window)
{
    WindowOverrideGuard override_guard(250);
    EXPECT_EQ(250u, og::sim::LinkLossWindow::window_ms());
    og::sim::LinkLossWindow window;
    const Clock::time_point t0 = Clock::now();
    window.observe(true, t0);
    window.observe(false, t0);
    EXPECT_FALSE(window.expired(t0 + 249ms));
    EXPECT_TRUE(window.expired(t0 + 250ms));
}
