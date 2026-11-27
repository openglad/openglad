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

// Do the keys held (one bit a level, key_level_mask) hold the key of `level`? The one test of a
// door's key: og::reach::door_bars (beside the door arm of ob_pass_check) and the key holder's
// cheap test of og::reach::opens_way (Gameplay Fixes BK) both ask it.
inline bool holds_key_of_level(std::uint32_t keys, std::int32_t level)
{
    return (keys & static_cast<std::uint32_t>(key_level_mask(level))) != 0;
}
