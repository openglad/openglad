#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace og::sim {

using PeerId = std::uint32_t;

enum class NetMessageType : std::uint8_t {
    Hello = 1,
    InputMessage = 2,
    SnapshotMessage = 3,
    DeltaSnapshotMessage = 4,
    SimEventBatchMessage = 5,
    GameFlowEventBatchMessage = 6,
    LobbyMessage = 7,
    LobbyStateMessage = 8,
    InitialSetup = 9,
    ClientReady = 10,
    KeyframeRequest = 11,
    Heartbeat = 12,
    ExitPromptBroadcast = 13,
    ExitPromptResponse = 14,
    PauseBroadcast = 15,
    PauseResponse = 16,
    ControlChange = 17,
};

constexpr std::uint8_t net_message_type_value(NetMessageType message_type) noexcept
{
    return static_cast<std::uint8_t>(message_type);
}

inline constexpr std::uint8_t kNetworkProtocolVersion = 1;
inline constexpr std::size_t kTransportHeaderSize = 4;
inline constexpr std::uint8_t kHelloMessageType =
    net_message_type_value(NetMessageType::Hello);
inline constexpr std::uint8_t kInputMessageType =
    net_message_type_value(NetMessageType::InputMessage);
inline constexpr std::uint8_t kInputStateMessageType = kInputMessageType;
inline constexpr std::uint8_t kSnapshotMessageType =
    net_message_type_value(NetMessageType::SnapshotMessage);
inline constexpr std::uint8_t kDeltaSnapshotMessageType =
    net_message_type_value(NetMessageType::DeltaSnapshotMessage);
inline constexpr std::uint8_t kSimEventBatchMessageType =
    net_message_type_value(NetMessageType::SimEventBatchMessage);
inline constexpr std::uint8_t kGameFlowEventBatchMessageType =
    net_message_type_value(NetMessageType::GameFlowEventBatchMessage);
inline constexpr std::uint8_t kLobbyMessageType =
    net_message_type_value(NetMessageType::LobbyMessage);
inline constexpr std::uint8_t kLobbyStateMessageType =
    net_message_type_value(NetMessageType::LobbyStateMessage);
inline constexpr std::uint8_t kInitialSetupMessageType =
    net_message_type_value(NetMessageType::InitialSetup);
inline constexpr std::uint8_t kClientReadyMessageType =
    net_message_type_value(NetMessageType::ClientReady);
inline constexpr std::uint8_t kKeyframeRequestMessageType =
    net_message_type_value(NetMessageType::KeyframeRequest);
inline constexpr std::uint8_t kHeartbeatMessageType =
    net_message_type_value(NetMessageType::Heartbeat);
inline constexpr std::uint8_t kExitPromptBroadcastMessageType =
    net_message_type_value(NetMessageType::ExitPromptBroadcast);
inline constexpr std::uint8_t kExitPromptResponseMessageType =
    net_message_type_value(NetMessageType::ExitPromptResponse);
inline constexpr std::uint8_t kPauseBroadcastMessageType =
    net_message_type_value(NetMessageType::PauseBroadcast);
inline constexpr std::uint8_t kPauseResponseMessageType =
    net_message_type_value(NetMessageType::PauseResponse);
inline constexpr std::uint8_t kControlChangeMessageType =
    net_message_type_value(NetMessageType::ControlChange);

struct ReceivedMessage {
    PeerId peer_id = 0;
    std::vector<std::uint8_t> data;
};

class ITransport {
public:
    virtual ~ITransport() = default;

    virtual void send(PeerId peer_id,
                      const std::uint8_t* data,
                      std::size_t len) = 0;

    void send(PeerId peer_id, std::span<const std::uint8_t> bytes)
    {
        send(peer_id, bytes.data(), bytes.size());
    }

    [[nodiscard]] virtual std::vector<ReceivedMessage> poll() = 0;
    virtual void accept_connections() = 0;
    virtual void disconnect(PeerId peer_id) = 0;
    [[nodiscard]] virtual std::vector<PeerId> connected_peers() const = 0;
};

struct TransportEnvelope {
    std::uint8_t protocol_version = kNetworkProtocolVersion;
    std::uint8_t message_type = 0;
    std::uint16_t payload_length = 0;
};

inline void append_transport_header(std::vector<std::uint8_t>& bytes,
                                    std::uint8_t message_type,
                                    std::uint16_t payload_length)
{
    bytes.push_back(kNetworkProtocolVersion);
    bytes.push_back(message_type);
    bytes.push_back(static_cast<std::uint8_t>(payload_length & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((payload_length >> 8) & 0xffu));
}

template <std::size_t N>
inline void write_transport_header(std::array<std::uint8_t, N>& bytes,
                                   std::uint8_t message_type,
                                   std::uint16_t payload_length)
{
    static_assert(N >= kTransportHeaderSize);
    bytes[0] = kNetworkProtocolVersion;
    bytes[1] = message_type;
    bytes[2] = static_cast<std::uint8_t>(payload_length & 0xffu);
    bytes[3] = static_cast<std::uint8_t>((payload_length >> 8) & 0xffu);
}

inline bool decode_transport_envelope(std::span<const std::uint8_t> bytes,
                                      TransportEnvelope& envelope) noexcept
{
    if (bytes.size() < kTransportHeaderSize ||
        bytes[0] != kNetworkProtocolVersion)
    {
        return false;
    }

    envelope.protocol_version = bytes[0];
    envelope.message_type = bytes[1];
    const std::uint16_t payload_lo = static_cast<std::uint16_t>(bytes[2]);
    const std::uint16_t payload_hi =
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[3]) << 8);
    envelope.payload_length =
        static_cast<std::uint16_t>(payload_lo | payload_hi);
    return true;
}

} // namespace og::sim
