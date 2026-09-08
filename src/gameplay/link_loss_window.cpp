#include <openglad/gameplay/link_loss_window.h>

#include <openglad/gameplay/net_constants.h>

#include <algorithm>

namespace og::sim {

std::uint64_t g_test_link_loss_window_ms_override = 0;

void LinkLossWindow::observe(bool connected, Clock::time_point now) noexcept
{
    if (connected)
    {
        connected_ = true;
        ever_connected_ = true;
        lost_since_.reset();
        return;
    }
    // Stamps the moment the link was first SEEN down after having been up —
    // on the connected→down transition, and on the first down poll after a
    // reset that kept ever_connected (a backdate, a re-observed link).
    if (ever_connected_ && !lost_since_.has_value())
        lost_since_ = now;
    connected_ = false;
}

void LinkLossWindow::reset() noexcept
{
    connected_ = false;
    ever_connected_ = false;
    lost_since_.reset();
}

bool LinkLossWindow::expired(Clock::time_point now) const noexcept
{
    if (!lost() || !lost_since_.has_value())
        return false;
    return now - *lost_since_ >= std::chrono::milliseconds(window_ms());
}

void LinkLossWindow::backdate_loss(float elapsed_ms, Clock::time_point now) noexcept
{
    ever_connected_ = true;
    lost_since_ = now -
        std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<float, std::milli>(
                std::max(elapsed_ms, 0.0f)));
}

std::uint64_t LinkLossWindow::window_ms() noexcept
{
    if (g_test_link_loss_window_ms_override != 0)
        return g_test_link_loss_window_ms_override;
    return CLIENT_CONNECTION_LOST_TIMEOUT_MS;
}

} // namespace og::sim
