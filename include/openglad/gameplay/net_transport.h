#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

struct InputState;

namespace og::sim {

struct SimEventBatch;
struct WorldSnapshot;

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
inline constexpr std::size_t kSessionTokenSize = 16;
using SessionToken = std::array<std::uint8_t, kSessionTokenSize>;
inline constexpr SessionToken kZeroSessionToken = {};
inline constexpr std::size_t kSerializedHelloPayloadSize =
    3 + kSessionTokenSize + sizeof(std::uint32_t);
inline constexpr std::size_t kSerializedHelloMessageSize =
    kTransportHeaderSize + kSerializedHelloPayloadSize;
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

// Hello payload wire format:
// - byte 0: current protocol version
// - byte 1: minimum supported protocol version
// - byte 2: snapshot format version
// - bytes 3-18: session token
// - bytes 19-22: little-endian campaign content hash
struct HelloMessage {
    std::uint8_t protocol_version = kNetworkProtocolVersion;
    std::uint8_t min_protocol_version = kNetworkProtocolVersion;
    std::uint8_t snapshot_format_version = 0;
    SessionToken session_token = {};
    std::uint32_t campaign_content_hash = 0;
};

struct ReceivedMessage {
    PeerId peer_id = 0;
    std::vector<std::uint8_t> data;
};

enum class TypedReceivedMessageKind : std::uint8_t {
    Snapshot,
    DeltaSnapshot,
    Input,
    SimEventBatch,
    GameFlowEventBatch,
};

struct TypedReceivedMessage {
    PeerId peer_id = 0;
    TypedReceivedMessageKind kind = TypedReceivedMessageKind::Snapshot;
    std::shared_ptr<WorldSnapshot> snapshot;
    std::shared_ptr<InputState> input;
    std::shared_ptr<SimEventBatch> event_batch;
    std::uint32_t tick = 0;
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

    [[nodiscard]] virtual bool supports_typed_messages() const noexcept;
    virtual void send_snapshot(PeerId peer_id,
                               std::shared_ptr<WorldSnapshot> snapshot);
    virtual void send_delta_snapshot(PeerId peer_id,
                                     std::shared_ptr<WorldSnapshot> snapshot);
    virtual void send_input(PeerId peer_id,
                            std::shared_ptr<InputState> input,
                            std::uint32_t tick);
    void send_input(PeerId peer_id, std::shared_ptr<InputState> input)
    {
        send_input(peer_id, std::move(input), 0);
    }
    virtual void send_sim_event_batch(PeerId peer_id,
                                      std::shared_ptr<SimEventBatch> batch);
    virtual void send_game_flow_event_batch(PeerId peer_id,
                                            std::shared_ptr<SimEventBatch> batch);

    [[nodiscard]] virtual std::vector<ReceivedMessage> poll() = 0;
    [[nodiscard]] virtual std::vector<TypedReceivedMessage> poll_typed();
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

inline std::array<std::uint8_t, kSerializedHelloMessageSize>
serialize_hello(const HelloMessage& message)
{
    std::array<std::uint8_t, kSerializedHelloMessageSize> bytes{};
    write_transport_header(bytes, kHelloMessageType,
                           static_cast<std::uint16_t>(kSerializedHelloPayloadSize));

    bytes[kTransportHeaderSize + 0] = message.protocol_version;
    bytes[kTransportHeaderSize + 1] = message.min_protocol_version;
    bytes[kTransportHeaderSize + 2] = message.snapshot_format_version;

    for (std::size_t i = 0; i < kSessionTokenSize; ++i)
    {
        bytes[kTransportHeaderSize + 3 + i] = message.session_token[i];
    }

    const std::size_t hash_offset = kTransportHeaderSize + 3 + kSessionTokenSize;
    bytes[hash_offset + 0] =
        static_cast<std::uint8_t>(message.campaign_content_hash & 0xffu);
    bytes[hash_offset + 1] = static_cast<std::uint8_t>(
        (message.campaign_content_hash >> 8) & 0xffu);
    bytes[hash_offset + 2] = static_cast<std::uint8_t>(
        (message.campaign_content_hash >> 16) & 0xffu);
    bytes[hash_offset + 3] = static_cast<std::uint8_t>(
        (message.campaign_content_hash >> 24) & 0xffu);

    return bytes;
}

inline std::optional<HelloMessage> deserialize_hello_message(
    std::span<const std::uint8_t> bytes)
{
    if (bytes.size() != kSerializedHelloMessageSize)
    {
        return std::nullopt;
    }

    TransportEnvelope envelope;
    if (!decode_transport_envelope(bytes, envelope) ||
        envelope.message_type != kHelloMessageType ||
        envelope.payload_length != kSerializedHelloPayloadSize)
    {
        return std::nullopt;
    }

    HelloMessage message;
    message.protocol_version = bytes[kTransportHeaderSize + 0];
    message.min_protocol_version = bytes[kTransportHeaderSize + 1];
    message.snapshot_format_version = bytes[kTransportHeaderSize + 2];

    if (message.protocol_version != envelope.protocol_version ||
        message.min_protocol_version > message.protocol_version)
    {
        return std::nullopt;
    }

    for (std::size_t i = 0; i < kSessionTokenSize; ++i)
    {
        message.session_token[i] = bytes[kTransportHeaderSize + 3 + i];
    }

    const std::size_t hash_offset = kTransportHeaderSize + 3 + kSessionTokenSize;
    message.campaign_content_hash =
        static_cast<std::uint32_t>(bytes[hash_offset + 0]) |
        (static_cast<std::uint32_t>(bytes[hash_offset + 1]) << 8) |
        (static_cast<std::uint32_t>(bytes[hash_offset + 2]) << 16) |
        (static_cast<std::uint32_t>(bytes[hash_offset + 3]) << 24);
    return message;
}

} // namespace og::sim
