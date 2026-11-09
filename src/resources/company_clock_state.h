#pragma once

#include <cstdint>
#include <optional>

namespace og::data::detail {
// Shared clock state: production reads it; the test support library sets it.
std::optional<std::int64_t>& company_clock_override();
}
