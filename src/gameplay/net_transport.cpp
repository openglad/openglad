#include <openglad/gameplay/net_transport.h>

#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/world_snapshot.h>

#include <cstring>
#include <functional>
#include <limits>
#include <stdexcept>

namespace {

void append_u8(std::vector<std::uint8_t>& bytes, std::uint8_t value)
{
    bytes.push_back(value);
}

void append_bool(std::vector<std::uint8_t>& bytes, bool value)
{
    append_u8(bytes, value ? 1U : 0U);
}

void append_u32(std::vector<std::uint8_t>& bytes, std::uint32_t value)
{
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24) & 0xffu));
}

void append_i16(std::vector<std::uint8_t>& bytes, std::int16_t value)
{
    const std::uint16_t raw = static_cast<std::uint16_t>(value);
    bytes.push_back(static_cast<std::uint8_t>(raw & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 8) & 0xffu));
}

void append_i32(std::vector<std::uint8_t>& bytes, std::int32_t value)
{
    append_u32(bytes, static_cast<std::uint32_t>(value));
}

void append_f32(std::vector<std::uint8_t>& bytes, float value)
{
    const auto* raw = reinterpret_cast<const std::uint8_t*>(&value);
    bytes.insert(bytes.end(), raw, raw + sizeof(value));
}

void append_string(std::vector<std::uint8_t>& bytes, const std::string& value)
{
    if (value.size() > std::numeric_limits<std::uint32_t>::max())
    {
        throw std::runtime_error("transport string exceeds maximum size");
    }

    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    bytes.insert(bytes.end(), value.begin(), value.end());
}

std::vector<std::uint8_t> wrap_transport_message(
    std::uint8_t message_type,
    const std::vector<std::uint8_t>& payload)
{
    if (payload.size() > std::numeric_limits<std::uint16_t>::max())
    {
        throw std::runtime_error("transport payload exceeds maximum size");
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(og::sim::kTransportHeaderSize + payload.size());
    og::sim::append_transport_header(
        bytes,
        message_type,
        static_cast<std::uint16_t>(payload.size()));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

std::optional<std::span<const std::uint8_t>> payload_span(
    std::span<const std::uint8_t> bytes,
    std::uint8_t expected_type)
{
    og::sim::TransportEnvelope envelope;
    if (!og::sim::decode_transport_envelope(bytes, envelope) ||
        envelope.message_type != expected_type ||
        bytes.size() != og::sim::kTransportHeaderSize + envelope.payload_length)
    {
        return std::nullopt;
    }

    return bytes.subspan(og::sim::kTransportHeaderSize, envelope.payload_length);
}

class PayloadReader
{
public:
    explicit PayloadReader(std::span<const std::uint8_t> payload)
        : payload_(payload)
    {
    }

    [[nodiscard]] bool ok() const noexcept { return ok_; }
    [[nodiscard]] bool finished() const noexcept
    {
        return ok_ && offset_ == payload_.size();
    }

    std::uint8_t read_u8()
    {
        if (offset_ + 1 > payload_.size())
        {
            ok_ = false;
            return 0;
        }
        return payload_[offset_++];
    }

    bool read_bool()
    {
        return read_u8() != 0;
    }

    std::uint32_t read_u32()
    {
        if (offset_ + 4 > payload_.size())
        {
            ok_ = false;
            return 0;
        }

        const std::uint32_t value =
            static_cast<std::uint32_t>(payload_[offset_]) |
            (static_cast<std::uint32_t>(payload_[offset_ + 1]) << 8) |
            (static_cast<std::uint32_t>(payload_[offset_ + 2]) << 16) |
            (static_cast<std::uint32_t>(payload_[offset_ + 3]) << 24);
        offset_ += 4;
        return value;
    }

    std::int16_t read_i16()
    {
        if (offset_ + 2 > payload_.size())
        {
            ok_ = false;
            return 0;
        }

        const std::uint16_t value =
            static_cast<std::uint16_t>(payload_[offset_]) |
            (static_cast<std::uint16_t>(payload_[offset_ + 1]) << 8);
        offset_ += 2;
        return static_cast<std::int16_t>(value);
    }

    std::int32_t read_i32()
    {
        return static_cast<std::int32_t>(read_u32());
    }

    float read_f32()
    {
        if (offset_ + sizeof(float) > payload_.size())
        {
            ok_ = false;
            return 0.0f;
        }

        float value = 0.0f;
        std::memcpy(&value, payload_.data() + offset_, sizeof(value));
        offset_ += sizeof(value);
        return value;
    }

    std::string read_string()
    {
        const std::uint32_t size = read_u32();
        if (!ok_ || offset_ + size > payload_.size())
        {
            ok_ = false;
            return {};
        }

        std::string value(payload_.begin() + static_cast<std::ptrdiff_t>(offset_),
                          payload_.begin() +
                              static_cast<std::ptrdiff_t>(offset_ + size));
        offset_ += size;
        return value;
    }

private:
    std::span<const std::uint8_t> payload_;
    std::size_t offset_ = 0;
    bool ok_ = true;
};

void append_initial_setup_guy(std::vector<std::uint8_t>& payload,
                              const og::sim::InitialSetupGuyData& guy)
{
    append_i32(payload, guy.guy_id);
    append_string(payload, guy.name);
    append_u8(payload, static_cast<std::uint8_t>(guy.family));
    append_i16(payload, guy.strength);
    append_i16(payload, guy.dexterity);
    append_i16(payload, guy.constitution);
    append_i16(payload, guy.intelligence);
    append_i16(payload, guy.armor);
    append_u32(payload, guy.exp);
    append_i16(payload, guy.kills);
    append_i32(payload, guy.level_kills);
    append_i32(payload, guy.total_damage);
    append_i32(payload, guy.total_hits);
    append_i32(payload, guy.total_shots);
    append_i16(payload, guy.teamnum);
    append_f32(payload, guy.scen_damage);
    append_i16(payload, guy.scen_kills);
    append_f32(payload, guy.scen_damage_taken);
    append_f32(payload, guy.scen_min_hp);
    append_i16(payload, guy.scen_shots);
    append_i16(payload, guy.scen_hits);
    append_i16(payload, guy.level);
}

og::sim::InitialSetupGuyData read_initial_setup_guy(PayloadReader& reader)
{
    og::sim::InitialSetupGuyData guy;
    guy.guy_id = reader.read_i32();
    guy.name = reader.read_string();
    guy.family = static_cast<std::int8_t>(reader.read_u8());
    guy.strength = reader.read_i16();
    guy.dexterity = reader.read_i16();
    guy.constitution = reader.read_i16();
    guy.intelligence = reader.read_i16();
    guy.armor = reader.read_i16();
    guy.exp = reader.read_u32();
    guy.kills = reader.read_i16();
    guy.level_kills = reader.read_i32();
    guy.total_damage = reader.read_i32();
    guy.total_hits = reader.read_i32();
    guy.total_shots = reader.read_i32();
    guy.teamnum = reader.read_i16();
    guy.scen_damage = reader.read_f32();
    guy.scen_kills = reader.read_i16();
    guy.scen_damage_taken = reader.read_f32();
    guy.scen_min_hp = reader.read_f32();
    guy.scen_shots = reader.read_i16();
    guy.scen_hits = reader.read_i16();
    guy.level = reader.read_i16();
    return guy;
}

template <typename Message>
std::optional<Message> deserialize_message(
    std::span<const std::uint8_t> bytes,
    std::uint8_t expected_type,
    const std::function<void(PayloadReader&, Message&)>& decode)
{
    const std::optional<std::span<const std::uint8_t>> payload =
        payload_span(bytes, expected_type);
    if (!payload.has_value())
        return std::nullopt;

    PayloadReader reader(*payload);
    Message message;
    decode(reader, message);
    if (!reader.finished())
        return std::nullopt;
    return message;
}

} // namespace

namespace og::sim {

std::vector<std::uint8_t> serialize_initial_setup_message(
    const InitialSetupMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_i32(payload, message.level_id);
    append_string(payload, message.level_title);
    append_u8(payload, static_cast<std::uint8_t>(message.level_type));
    append_i16(payload, message.par_value);
    append_i16(payload, message.time_bonus_limit);
    append_i16(payload, message.difficulty);
    append_i32(payload, message.pixmaxx);
    append_i32(payload, message.pixmaxy);
    append_i16(payload, message.my_team);
    append_i16(payload, message.allied_mode);
    append_i16(payload, message.current_scenario);
    append_u32(payload, static_cast<std::uint32_t>(message.guys.size()));
    for (const auto& guy : message.guys)
        append_initial_setup_guy(payload, guy);
    append_u32(payload, static_cast<std::uint32_t>(message.completed_levels.size()));
    for (const std::int32_t level_id : message.completed_levels)
        append_i32(payload, level_id);
    for (const std::uint32_t entity_id : message.controlled_entity_ids)
        append_u32(payload, entity_id);
    return wrap_transport_message(kInitialSetupMessageType, payload);
}

std::optional<InitialSetupMessage> deserialize_initial_setup_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<InitialSetupMessage>(
        bytes, kInitialSetupMessageType,
        [](PayloadReader& reader, InitialSetupMessage& message) {
            message.level_id = reader.read_i32();
            message.level_title = reader.read_string();
            message.level_type = static_cast<std::int8_t>(reader.read_u8());
            message.par_value = reader.read_i16();
            message.time_bonus_limit = reader.read_i16();
            message.difficulty = reader.read_i16();
            message.pixmaxx = reader.read_i32();
            message.pixmaxy = reader.read_i32();
            message.my_team = reader.read_i16();
            message.allied_mode = reader.read_i16();
            message.current_scenario = reader.read_i16();
            const std::uint32_t guy_count = reader.read_u32();
            message.guys.clear();
            message.guys.reserve(guy_count);
            for (std::uint32_t i = 0; i < guy_count; ++i)
                message.guys.push_back(read_initial_setup_guy(reader));
            const std::uint32_t level_count = reader.read_u32();
            message.completed_levels.clear();
            message.completed_levels.reserve(level_count);
            for (std::uint32_t i = 0; i < level_count; ++i)
                message.completed_levels.push_back(reader.read_i32());
            for (std::uint32_t& entity_id : message.controlled_entity_ids)
                entity_id = reader.read_u32();
        });
}

std::vector<std::uint8_t> serialize_client_ready_message(
    const ClientReadyMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u32(payload, message.last_applied_tick);
    return wrap_transport_message(kClientReadyMessageType, payload);
}

std::optional<ClientReadyMessage> deserialize_client_ready_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<ClientReadyMessage>(
        bytes, kClientReadyMessageType,
        [](PayloadReader& reader, ClientReadyMessage& message) {
            message.last_applied_tick = reader.read_u32();
        });
}

std::vector<std::uint8_t> serialize_keyframe_request_message(
    const KeyframeRequestMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u32(payload, message.last_seen_tick);
    return wrap_transport_message(kKeyframeRequestMessageType, payload);
}

std::optional<KeyframeRequestMessage> deserialize_keyframe_request_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<KeyframeRequestMessage>(
        bytes, kKeyframeRequestMessageType,
        [](PayloadReader& reader, KeyframeRequestMessage& message) {
            message.last_seen_tick = reader.read_u32();
        });
}

std::vector<std::uint8_t> serialize_exit_prompt_broadcast_message(
    const ExitPromptBroadcastMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_i16(payload, message.destination_level);
    append_bool(payload, message.withdraw_prompt);
    append_string(payload, message.prompt_text);
    return wrap_transport_message(kExitPromptBroadcastMessageType, payload);
}

std::optional<ExitPromptBroadcastMessage>
deserialize_exit_prompt_broadcast_message(std::span<const std::uint8_t> bytes)
{
    return deserialize_message<ExitPromptBroadcastMessage>(
        bytes, kExitPromptBroadcastMessageType,
        [](PayloadReader& reader, ExitPromptBroadcastMessage& message) {
            message.destination_level = reader.read_i16();
            message.withdraw_prompt = reader.read_bool();
            message.prompt_text = reader.read_string();
        });
}

std::vector<std::uint8_t> serialize_exit_prompt_response_message(
    const ExitPromptResponseMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_bool(payload, message.accepted);
    return wrap_transport_message(kExitPromptResponseMessageType, payload);
}

std::optional<ExitPromptResponseMessage>
deserialize_exit_prompt_response_message(std::span<const std::uint8_t> bytes)
{
    return deserialize_message<ExitPromptResponseMessage>(
        bytes, kExitPromptResponseMessageType,
        [](PayloadReader& reader, ExitPromptResponseMessage& message) {
            message.accepted = reader.read_bool();
        });
}

std::vector<std::uint8_t> serialize_pause_broadcast_message(
    const PauseBroadcastMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u8(payload, message.player_index);
    append_string(payload, message.player_name);
    return wrap_transport_message(kPauseBroadcastMessageType, payload);
}

std::optional<PauseBroadcastMessage> deserialize_pause_broadcast_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<PauseBroadcastMessage>(
        bytes, kPauseBroadcastMessageType,
        [](PayloadReader& reader, PauseBroadcastMessage& message) {
            message.player_index = reader.read_u8();
            message.player_name = reader.read_string();
        });
}

std::vector<std::uint8_t> serialize_pause_response_message(
    const PauseResponseMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_bool(payload, message.resume);
    return wrap_transport_message(kPauseResponseMessageType, payload);
}

std::optional<PauseResponseMessage> deserialize_pause_response_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<PauseResponseMessage>(
        bytes, kPauseResponseMessageType,
        [](PayloadReader& reader, PauseResponseMessage& message) {
            message.resume = reader.read_bool();
        });
}

std::vector<std::uint8_t> serialize_control_change_message(
    const ControlChangeMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u8(payload, message.player_index);
    append_u32(payload, message.entity_id);
    return wrap_transport_message(kControlChangeMessageType, payload);
}

std::optional<ControlChangeMessage> deserialize_control_change_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<ControlChangeMessage>(
        bytes, kControlChangeMessageType,
        [](PayloadReader& reader, ControlChangeMessage& message) {
            message.player_index = reader.read_u8();
            message.entity_id = reader.read_u32();
        });
}

std::vector<std::uint8_t> serialize_snapshot_hash_check_message(
    const SnapshotHashCheckMessage& message)
{
    std::vector<std::uint8_t> payload;
    append_u32(payload, message.tick);
    append_u32(payload, message.snapshot_hash);
    return wrap_transport_message(kSnapshotHashCheckMessageType, payload);
}

std::optional<SnapshotHashCheckMessage> deserialize_snapshot_hash_check_message(
    std::span<const std::uint8_t> bytes)
{
    return deserialize_message<SnapshotHashCheckMessage>(
        bytes, kSnapshotHashCheckMessageType,
        [](PayloadReader& reader, SnapshotHashCheckMessage& message) {
            message.tick = reader.read_u32();
            message.snapshot_hash = reader.read_u32();
        });
}

bool ITransport::supports_typed_messages() const noexcept
{
    return false;
}

void ITransport::send_snapshot(PeerId peer_id,
                               std::shared_ptr<WorldSnapshot> snapshot)
{
    if (!snapshot)
        return;

    const std::vector<std::uint8_t> bytes = serialize_snapshot(*snapshot);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_delta_snapshot(PeerId peer_id,
                                     std::shared_ptr<WorldSnapshot> snapshot)
{
    if (!snapshot)
        return;

    const std::vector<std::uint8_t> bytes = serialize_delta(*snapshot);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_input(PeerId peer_id,
                            std::shared_ptr<InputState> input,
                            std::uint32_t tick)
{
    if (!input)
        return;

    const auto bytes = serialize_input(tick, *input);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_sim_event_batch(PeerId peer_id,
                                      std::shared_ptr<SimEventBatch> batch)
{
    if (!batch)
        return;

    const std::vector<std::uint8_t> bytes = serialize_sim_event_batch(*batch);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_game_flow_event_batch(
    PeerId peer_id,
    std::shared_ptr<SimEventBatch> batch)
{
    if (!batch)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_game_flow_event_batch(*batch);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_initial_setup(PeerId peer_id,
                                    std::shared_ptr<InitialSetupMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_initial_setup_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_client_ready(PeerId peer_id,
                                   std::shared_ptr<ClientReadyMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_client_ready_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_keyframe_request(
    PeerId peer_id,
    std::shared_ptr<KeyframeRequestMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_keyframe_request_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_exit_prompt_broadcast(
    PeerId peer_id,
    std::shared_ptr<ExitPromptBroadcastMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_exit_prompt_broadcast_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_exit_prompt_response(
    PeerId peer_id,
    std::shared_ptr<ExitPromptResponseMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_exit_prompt_response_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_pause_broadcast(
    PeerId peer_id,
    std::shared_ptr<PauseBroadcastMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_pause_broadcast_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_pause_response(
    PeerId peer_id,
    std::shared_ptr<PauseResponseMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_pause_response_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_control_change(
    PeerId peer_id,
    std::shared_ptr<ControlChangeMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_control_change_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

void ITransport::send_snapshot_hash_check(
    PeerId peer_id,
    std::shared_ptr<SnapshotHashCheckMessage> message)
{
    if (!message)
        return;

    const std::vector<std::uint8_t> bytes =
        serialize_snapshot_hash_check_message(*message);
    send(peer_id, bytes.data(), bytes.size());
}

std::vector<TypedReceivedMessage> ITransport::poll_typed()
{
    return {};
}

} // namespace og::sim
