#pragma once

#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/net_transport.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace og::sim {

inline constexpr std::uint8_t kInputStateProtocolVersion = kNetworkProtocolVersion;
inline constexpr std::size_t kSerializedInputPayloadSize = 21;
inline constexpr std::size_t kSerializedInputMessageSize =
    kTransportHeaderSize + kSerializedInputPayloadSize;
inline constexpr std::size_t kSerializedInputStateSize =
    kSerializedInputMessageSize;

struct InputStateMessage {
    std::uint32_t tick = 0;
    InputState input = {};
};

// Wire format:
// - bytes 0-3: shared transport envelope
// - bytes 4-7: little-endian tick
// - byte 8: quit_requested
// - bytes 9-24: four little-endian player bitfields; bits 0-15 map held[],
//   bits 16-31 map pressed[].
std::array<std::uint8_t, kSerializedInputMessageSize>
serialize_input(const InputState& input);

std::array<std::uint8_t, kSerializedInputMessageSize>
serialize_input(std::uint32_t tick, const InputState& input);

std::optional<InputStateMessage>
deserialize_input_message(std::span<const std::uint8_t> bytes);

std::optional<InputState> deserialize_input(std::span<const std::uint8_t> bytes);

} // namespace og::sim
