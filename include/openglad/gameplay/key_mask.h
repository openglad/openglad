/* Deterministic key-level bitmask helper shared by gameplay door/key logic.
 *
 * Key levels are small in practice, but clamp defensively to avoid UB from
 * oversized shifts when loading malformed or future content.
 */
#pragma once

#include <algorithm>
#include <cstdint>

inline std::int32_t key_level_mask(std::int32_t level)
{
    constexpr std::int32_t kMaxDeterministicKeyLevel = 30;
    const std::int32_t clamped_level = std::clamp(level, 0, kMaxDeterministicKeyLevel);
    return static_cast<std::int32_t>(1u << static_cast<unsigned int>(clamped_level));
}
