#pragma once

#include <openglad/gameplay/input_state.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace og::sim {

inline constexpr std::uint8_t kInputStateProtocolVersion = 1;
inline constexpr std::size_t kSerializedInputStateSize = 17;

// Wire format:
// - byte 0: bits 1-7 protocol version, bit 0 quit_requested
// - bytes 1-16: four little-endian player bitfields; bits 0-15 map held[],
//   bits 16-31 map pressed[].
std::array<std::uint8_t, kSerializedInputStateSize>
serialize_input(const InputState& input);

std::optional<InputState> deserialize_input(std::span<const std::uint8_t> bytes);

} // namespace og::sim
