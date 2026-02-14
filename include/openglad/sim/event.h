#pragma once

#include <cstdint>

namespace og::sim {

// Minimal deterministic event format for headless simulation tests.
// This is intentionally POD-ish so event streams can be compared byte-for-byte.
struct Event final {
    std::uint32_t tick = 0;
    std::uint32_t kind = 0;
    std::uint32_t a = 0;
    std::uint32_t b = 0;
};

} // namespace og::sim

