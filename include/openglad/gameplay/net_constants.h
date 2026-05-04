#pragma once

#include <cstddef>
#include <cstdint>

namespace og::sim {

inline constexpr int DEFAULT_SIM_TICKS_PER_SEC = 12;
inline constexpr int DEFAULT_SIM_TICK_MS = 1000 / DEFAULT_SIM_TICKS_PER_SEC;
inline constexpr float TIMER_WAIT_TO_MS = 13.6f;
inline constexpr signed char DEFAULT_TIMER_WAIT = 6;
inline constexpr signed char MIN_TIMER_WAIT = 1;
inline constexpr signed char MAX_TIMER_WAIT = 20;
inline constexpr int KEYFRAME_INTERVAL_TICKS = DEFAULT_SIM_TICKS_PER_SEC * 5;
inline constexpr int MAX_LATE_PRESS_TICKS = 2;
inline constexpr std::uint64_t DISCONNECT_TIMEOUT_MS = 10'000;
inline constexpr std::uint64_t CLIENT_CONNECTION_LOST_TIMEOUT_MS = 30'000;
inline constexpr std::uint64_t EXIT_PROMPT_TIMEOUT_MS = 15'000;
inline constexpr std::uint64_t PAUSE_TIMEOUT_MS = 60'000;
inline constexpr std::uint64_t PAUSE_RATE_LIMIT_MS = 5'000;
inline constexpr std::size_t MAX_GRID_DIRTY_TILES = 64;
inline constexpr int MAX_INBOUND_MESSAGES_PER_TICK = 64;
inline constexpr int MAX_OUTBOUND_MESSAGES_PER_TICK = 32;
inline constexpr std::uint32_t MAX_NETWORK_BUDGET_US_PER_TICK = 2000; // 2 ms (advisory, traced not enforced)

} // namespace og::sim
