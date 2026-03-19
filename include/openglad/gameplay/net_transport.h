#pragma once

#include <openglad/gameplay/input_state.h>
#include <openglad/gameplay/lobby_state.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

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
    SnapshotHashCheck = 18,
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
inline bool is_zero_session_token(const SessionToken& token) noexcept
{
    return token == kZeroSessionToken;
}
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
inline constexpr std::uint8_t kSnapshotHashCheckMessageType =
    net_message_type_value(NetMessageType::SnapshotHashCheck);

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

    bool operator==(const HelloMessage&) const = default;
};

struct InitialSetupGuyData {
    std::int32_t guy_id = 0;
    std::string name;
    std::int8_t family = 0;
    std::int16_t strength = 0;
    std::int16_t dexterity = 0;
    std::int16_t constitution = 0;
    std::int16_t intelligence = 0;
    std::int16_t armor = 0;
    std::uint32_t exp = 0;
    std::int16_t kills = 0;
    std::int32_t level_kills = 0;
    std::int32_t total_damage = 0;
    std::int32_t total_hits = 0;
    std::int32_t total_shots = 0;
    std::int16_t teamnum = 0;
    float scen_damage = 0.0f;
    std::int16_t scen_kills = 0;
    float scen_damage_taken = 0.0f;
    float scen_min_hp = 0.0f;
    std::int16_t scen_shots = 0;
    std::int16_t scen_hits = 0;
    std::int16_t level = 0;

    bool operator==(const InitialSetupGuyData&) const = default;
};

struct InitialSetupMessage {
    std::int32_t level_id = 0;
    std::string level_title;
    std::int8_t level_type = 0;
    std::int16_t par_value = 0;
    std::int16_t time_bonus_limit = 0;
    std::int16_t difficulty = 0;
    std::int32_t pixmaxx = 0;
    std::int32_t pixmaxy = 0;
    std::int16_t my_team = 0;
    std::int16_t allied_mode = 0;
    std::int16_t current_scenario = 0;
    std::vector<InitialSetupGuyData> guys;
    std::vector<std::int32_t> completed_levels;
    std::array<std::uint32_t, MAX_PLAYERS> controlled_entity_ids = {};

    bool operator==(const InitialSetupMessage&) const = default;
};

struct ClientReadyMessage {
    std::uint32_t last_applied_tick = 0;

    bool operator==(const ClientReadyMessage&) const = default;
};

struct KeyframeRequestMessage {
    std::uint32_t last_seen_tick = 0;

    bool operator==(const KeyframeRequestMessage&) const = default;
};

struct HeartbeatMessage {
    bool operator==(const HeartbeatMessage&) const = default;
};

struct ExitPromptBroadcastMessage {
    std::int16_t destination_level = -1;
    bool withdraw_prompt = false;
    std::string prompt_text;

    bool operator==(const ExitPromptBroadcastMessage&) const = default;
};

struct ExitPromptResponseMessage {
    bool accepted = false;

    bool operator==(const ExitPromptResponseMessage&) const = default;
};

struct PauseBroadcastMessage {
    std::uint8_t player_index = 0xff;
    std::string player_name;

    bool operator==(const PauseBroadcastMessage&) const = default;
};

struct PauseResponseMessage {
    bool resume = true;

    bool operator==(const PauseResponseMessage&) const = default;
};

struct ControlChangeMessage {
    std::uint8_t player_index = 0xff;
    std::uint32_t entity_id = 0;

    bool operator==(const ControlChangeMessage&) const = default;
};

struct SnapshotHashCheckMessage {
    std::uint32_t tick = 0;
    std::uint32_t snapshot_hash = 0;

    bool operator==(const SnapshotHashCheckMessage&) const = default;
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
    LobbyMessage,
    LobbyState,
    InitialSetup,
    Hello,
    ClientReady,
    KeyframeRequest,
    Heartbeat,
    ExitPromptBroadcast,
    ExitPromptResponse,
    PauseBroadcast,
    PauseResponse,
    ControlChange,
    SnapshotHashCheck,
    Malformed,
};

struct TypedReceivedMessage {
    PeerId peer_id = 0;
    TypedReceivedMessageKind kind = TypedReceivedMessageKind::Snapshot;
    std::shared_ptr<WorldSnapshot> snapshot;
    std::shared_ptr<InputState> input;
    std::shared_ptr<SimEventBatch> event_batch;
    std::shared_ptr<LobbyMessage> lobby_message;
    std::shared_ptr<LobbyState> lobby_state;
    std::shared_ptr<InitialSetupMessage> initial_setup;
    std::shared_ptr<HelloMessage> hello;
    std::shared_ptr<ClientReadyMessage> client_ready;
    std::shared_ptr<KeyframeRequestMessage> keyframe_request;
    std::shared_ptr<HeartbeatMessage> heartbeat;
    std::shared_ptr<ExitPromptBroadcastMessage> exit_prompt_broadcast;
    std::shared_ptr<ExitPromptResponseMessage> exit_prompt_response;
    std::shared_ptr<PauseBroadcastMessage> pause_broadcast;
    std::shared_ptr<PauseResponseMessage> pause_response;
    std::shared_ptr<ControlChangeMessage> control_change;
    std::shared_ptr<SnapshotHashCheckMessage> snapshot_hash_check;
    std::uint32_t tick = 0;
};

std::vector<std::uint8_t> serialize_initial_setup_message(
    const InitialSetupMessage& message);
std::optional<InitialSetupMessage> deserialize_initial_setup_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_lobby_message(const LobbyMessage& message);
std::optional<LobbyMessage> deserialize_lobby_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_lobby_state_message(
    const LobbyState& state);
std::optional<LobbyState> deserialize_lobby_state_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_client_ready_message(
    const ClientReadyMessage& message);
std::optional<ClientReadyMessage> deserialize_client_ready_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_heartbeat_message(
    const HeartbeatMessage& message);
std::optional<HeartbeatMessage> deserialize_heartbeat_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_keyframe_request_message(
    const KeyframeRequestMessage& message);
std::optional<KeyframeRequestMessage> deserialize_keyframe_request_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_exit_prompt_broadcast_message(
    const ExitPromptBroadcastMessage& message);
std::optional<ExitPromptBroadcastMessage>
deserialize_exit_prompt_broadcast_message(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_exit_prompt_response_message(
    const ExitPromptResponseMessage& message);
std::optional<ExitPromptResponseMessage>
deserialize_exit_prompt_response_message(std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_pause_broadcast_message(
    const PauseBroadcastMessage& message);
std::optional<PauseBroadcastMessage> deserialize_pause_broadcast_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_pause_response_message(
    const PauseResponseMessage& message);
std::optional<PauseResponseMessage> deserialize_pause_response_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_control_change_message(
    const ControlChangeMessage& message);
std::optional<ControlChangeMessage> deserialize_control_change_message(
    std::span<const std::uint8_t> bytes);
std::vector<std::uint8_t> serialize_snapshot_hash_check_message(
    const SnapshotHashCheckMessage& message);
std::optional<SnapshotHashCheckMessage> deserialize_snapshot_hash_check_message(
    std::span<const std::uint8_t> bytes);

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
    virtual void send_lobby_message(PeerId peer_id,
                                    std::shared_ptr<LobbyMessage> message);
    virtual void send_lobby_state(PeerId peer_id,
                                  std::shared_ptr<LobbyState> state);
    virtual void send_initial_setup(PeerId peer_id,
                                    std::shared_ptr<InitialSetupMessage> message);
    virtual void send_hello(PeerId peer_id,
                            std::shared_ptr<HelloMessage> message);
    virtual void send_client_ready(PeerId peer_id,
                                   std::shared_ptr<ClientReadyMessage> message);
    virtual void send_keyframe_request(
        PeerId peer_id,
        std::shared_ptr<KeyframeRequestMessage> message);
    virtual void send_heartbeat(
        PeerId peer_id,
        std::shared_ptr<HeartbeatMessage> message);
    virtual void send_exit_prompt_broadcast(
        PeerId peer_id,
        std::shared_ptr<ExitPromptBroadcastMessage> message);
    virtual void send_exit_prompt_response(
        PeerId peer_id,
        std::shared_ptr<ExitPromptResponseMessage> message);
    virtual void send_pause_broadcast(
        PeerId peer_id,
        std::shared_ptr<PauseBroadcastMessage> message);
    virtual void send_pause_response(
        PeerId peer_id,
        std::shared_ptr<PauseResponseMessage> message);
    virtual void send_control_change(PeerId peer_id,
                                     std::shared_ptr<ControlChangeMessage> message);
    virtual void send_snapshot_hash_check(
        PeerId peer_id,
        std::shared_ptr<SnapshotHashCheckMessage> message);

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
