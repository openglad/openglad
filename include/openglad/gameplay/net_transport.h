#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace og::sim {

inline constexpr std::uint8_t kNetworkProtocolVersion = 1;
inline constexpr std::size_t kTransportHeaderSize = 8;
inline constexpr std::uint8_t kSnapshotMessageType = 1;
inline constexpr std::uint8_t kDeltaSnapshotMessageType = 2;
inline constexpr std::uint8_t kInputStateMessageType = 3;
inline constexpr std::uint8_t kTransportPayloadUncompressedFlag = 0x80;

struct TransportEnvelope {
    std::uint8_t header_type = 0;
    std::uint8_t message_type = 0;
    std::uint16_t payload_length = 0;
    std::uint32_t tick = 0;
};

inline void append_transport_header(std::vector<std::uint8_t>& bytes,
                                    std::uint8_t header_type,
                                    std::uint16_t payload_length,
                                    std::uint32_t tick)
{
    bytes.push_back(kNetworkProtocolVersion);
    bytes.push_back(header_type);
    bytes.push_back(static_cast<std::uint8_t>(payload_length & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((payload_length >> 8) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>(tick & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((tick >> 8) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((tick >> 16) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((tick >> 24) & 0xffu));
}

template <std::size_t N>
inline void write_transport_header(std::array<std::uint8_t, N>& bytes,
                                   std::uint8_t header_type,
                                   std::uint16_t payload_length,
                                   std::uint32_t tick)
{
    static_assert(N >= kTransportHeaderSize);
    bytes[0] = kNetworkProtocolVersion;
    bytes[1] = header_type;
    bytes[2] = static_cast<std::uint8_t>(payload_length & 0xffu);
    bytes[3] = static_cast<std::uint8_t>((payload_length >> 8) & 0xffu);
    bytes[4] = static_cast<std::uint8_t>(tick & 0xffu);
    bytes[5] = static_cast<std::uint8_t>((tick >> 8) & 0xffu);
    bytes[6] = static_cast<std::uint8_t>((tick >> 16) & 0xffu);
    bytes[7] = static_cast<std::uint8_t>((tick >> 24) & 0xffu);
}

inline bool decode_transport_envelope(std::span<const std::uint8_t> bytes,
                                      TransportEnvelope& envelope) noexcept
{
    if (bytes.size() < kTransportHeaderSize ||
        bytes[0] != kNetworkProtocolVersion)
    {
        return false;
    }

    envelope.header_type = bytes[1];
    envelope.message_type = static_cast<std::uint8_t>(
        bytes[1] & ~kTransportPayloadUncompressedFlag);
    const std::uint16_t payload_lo = static_cast<std::uint16_t>(bytes[2]);
    const std::uint16_t payload_hi =
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[3]) << 8);
    envelope.payload_length =
        static_cast<std::uint16_t>(payload_lo | payload_hi);

    const std::uint32_t tick_b0 = static_cast<std::uint32_t>(bytes[4]);
    const std::uint32_t tick_b1 =
        static_cast<std::uint32_t>(static_cast<std::uint32_t>(bytes[5]) << 8);
    const std::uint32_t tick_b2 =
        static_cast<std::uint32_t>(static_cast<std::uint32_t>(bytes[6]) << 16);
    const std::uint32_t tick_b3 =
        static_cast<std::uint32_t>(static_cast<std::uint32_t>(bytes[7]) << 24);
    envelope.tick = tick_b0 | tick_b1 | tick_b2 | tick_b3;
    return true;
}

} // namespace og::sim
