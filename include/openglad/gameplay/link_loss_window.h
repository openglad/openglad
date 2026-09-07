#pragma once

#include <chrono>
#include <cstdint>
#include <optional>

namespace og::sim {

// The ONE rule for a link that carried a session (#278). Fed the link state on
// every poll, it answers the two questions each consumer used to answer for
// itself:
//   lost()    — "this peer HAD a server and lost it": down now, after having
//               been up. What a display shows its stall banner for, what a
//               pause-menu QUIT treats as the connection-lost transition, what
//               the lobby's line B reports as "connection lost".
//   expired() — "dead for good": lost, and down for the whole reconnect window
//               CLIENT_CONNECTION_LOST_TIMEOUT_MS. The window is the allowance
//               an auto-reconnecting transport gets to come back inside the
//               server's seat-rebind grace (PAUSE_TIMEOUT_MS). The in-game
//               30 s backstop (GameClient) and the lobby-parked joiner's revert
//               (JoinPickerLobbyClient) both read it HERE, so the two phases
//               cannot disagree about when a session is over.
// A reconnect (observe(true)) reopens everything: lost() and expired() drop
// and the next loss starts a fresh window.
class LinkLossWindow {
public:
    using Clock = std::chrono::steady_clock;

    // The link state as of this poll.
    void observe(bool connected, Clock::time_point now) noexcept;
    // A fresh dial is a fresh timeline.
    void reset() noexcept;

    [[nodiscard]] bool connected() const noexcept
    {
        return connected_;
    }
    [[nodiscard]] bool ever_connected() const noexcept
    {
        return ever_connected_;
    }
    [[nodiscard]] bool lost() const noexcept
    {
        return !connected_ && ever_connected_;
    }
    [[nodiscard]] bool expired(Clock::time_point now) const noexcept;
    [[nodiscard]] std::optional<Clock::time_point> lost_since() const noexcept
    {
        return lost_since_;
    }

    // Tests: pretend the link went down `elapsed_ms` before `now` (the
    // connected flag is left alone — a link that is up is not lost).
    void backdate_loss(float elapsed_ms, Clock::time_point now) noexcept;

    // The window in force: CLIENT_CONNECTION_LOST_TIMEOUT_MS, or the TESTING
    // override when a test must see a real link exhaust it.
    [[nodiscard]] static std::uint64_t window_ms() noexcept;

private:
    bool connected_ = false;
    bool ever_connected_ = false;
    std::optional<Clock::time_point> lost_since_;
};

// Test hook, unconditional like g_test_level_tick_limit_override (the unit
// groups link the production component libraries): 0 = production window.
extern std::uint64_t g_test_link_loss_window_ms_override;

} // namespace og::sim
