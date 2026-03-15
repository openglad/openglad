#pragma once

#include <cstddef>

namespace og::sim {

inline constexpr int DEFAULT_SIM_TICKS_PER_SEC = 12;
inline constexpr int DEFAULT_SIM_TICK_MS = 1000 / DEFAULT_SIM_TICKS_PER_SEC;
inline constexpr float TIMER_WAIT_TO_MS = 13.6f;
inline constexpr signed char DEFAULT_TIMER_WAIT = 6;
inline constexpr signed char MIN_TIMER_WAIT = 1;
inline constexpr signed char MAX_TIMER_WAIT = 20;
inline constexpr std::size_t MAX_GRID_DIRTY_TILES = 64;

} // namespace og::sim
