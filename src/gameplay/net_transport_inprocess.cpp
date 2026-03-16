#include <openglad/gameplay/net_transport_inprocess.h>

#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/replay.h>
#include <openglad/gameplay/world_snapshot.h>

#include <algorithm>
#include <cstring>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

struct TransportValidationFailure {
    std::string field;
    std::string expected_value;
    std::string actual_value;
};

template <typename T>
std::string transport_value_to_string(const T& value)
{
    if constexpr (std::is_same_v<T, bool>)
    {
        return value ? "true" : "false";
    }
    else if constexpr (std::is_enum_v<T>)
    {
        return std::format("{}", static_cast<int>(value));
    }
    else if constexpr (std::is_same_v<T, std::uint8_t> ||
                       std::is_same_v<T, std::int8_t> ||
                       std::is_same_v<T, char> ||
                       std::is_same_v<T, signed char> ||
                       std::is_same_v<T, unsigned char>)
    {
        return std::format("{}", static_cast<int>(value));
    }
    else
    {
        return std::format("{}", value);
    }
}

template <typename T>
bool compare_transport_value(std::string_view field,
                             const T& expected,
                             const T& actual,
                             TransportValidationFailure& failure)
{
    if (expected == actual)
        return true;

    failure.field = std::string(field);
    failure.expected_value = transport_value_to_string(expected);
    failure.actual_value = transport_value_to_string(actual);
    return false;
}

std::optional<TransportValidationFailure> find_first_input_difference(
    std::uint32_t expected_tick,
    const InputState& expected,
    std::uint32_t actual_tick,
    const InputState& actual)
{
    TransportValidationFailure failure;
    if (!compare_transport_value("tick", expected_tick, actual_tick, failure))
        return failure;
    if (!compare_transport_value("quit_requested", expected.quit_requested,
                                 actual.quit_requested, failure))
    {
        return failure;
    }
    if (!compare_transport_value("timer_wait_request",
                                 expected.timer_wait_request,
                                 actual.timer_wait_request,
                                 failure))
    {
        return failure;
    }

    for (int player = 0; player < MAX_PLAYERS; ++player)
    {
        for (int key = 0; key < NUM_INPUT_KEYS; ++key)
        {
            if (!compare_transport_value(
                    std::format("players[{}].held[{}]", player, key),
                    expected.players[player].held[key],
                    actual.players[player].held[key],
                    failure))
            {
                return failure;
            }

            if (!compare_transport_value(
                    std::format("players[{}].pressed[{}]", player, key),
                    expected.players[player].pressed[key],
                    actual.players[player].pressed[key],
                    failure))
            {
                return failure;
            }
        }
    }

    return std::nullopt;
}

std::optional<TransportValidationFailure> find_first_event_batch_difference(
    const og::sim::SimEventBatch& expected,
    const og::sim::SimEventBatch& actual)
{
    TransportValidationFailure failure;
    if (!compare_transport_value("sequence", expected.sequence,
                                 actual.sequence, failure))
    {
        return failure;
    }
    if (!compare_transport_value("events.size", expected.events.size(),
                                 actual.events.size(), failure))
    {
        return failure;
    }

    for (std::size_t index = 0; index < expected.events.size(); ++index)
    {
        const og::sim::Event& expected_event = expected.events[index];
        const og::sim::Event& actual_event = actual.events[index];
        if (!compare_transport_value(std::format("events[{}].tick", index),
                                     expected_event.tick,
                                     actual_event.tick,
                                     failure))
        {
            return failure;
        }
        if (!compare_transport_value(std::format("events[{}].kind", index),
                                     expected_event.kind,
                                     actual_event.kind,
                                     failure))
        {
            return failure;
        }
        if (!compare_transport_value(std::format("events[{}].a", index),
                                     expected_event.a,
                                     actual_event.a,
                                     failure))
        {
            return failure;
        }
        if (!compare_transport_value(std::format("events[{}].b", index),
                                     expected_event.b,
                                     actual_event.b,
                                     failure))
        {
            return failure;
        }
        if (!compare_transport_value(std::format("events[{}].text", index),
                                     expected_event.text,
                                     actual_event.text,
                                     failure))
        {
            return failure;
        }
    }

    return std::nullopt;
}

bool dirty_mask_has_bit(const std::uint64_t* mask, std::uint8_t bit_index)
{
    return (mask[bit_index / 64] & (1ULL << (bit_index % 64))) != 0;
}

void copy_delta_entity_field(og::sim::EntitySnapshot& destination,
                             const og::sim::EntitySnapshot& source,
                             std::uint8_t bit_index)
{
    switch (bit_index)
    {
    case og::dirty::BIT_ENTITY_ID:
        destination.entity_id = source.entity_id;
        return;
    case og::dirty::BIT_REGEN_DELAY:
        destination.regen_delay = source.regen_delay;
        return;
    case og::dirty::BIT_SPECIAL_COST:
        std::copy(std::begin(source.special_cost), std::end(source.special_cost),
                  std::begin(destination.special_cost));
        return;
    case og::dirty::BIT_DO_BOUNCE:
        destination.do_bounce = source.do_bounce;
        return;
    default:
        break;
    }

    const auto* field = std::find_if(
        std::begin(og::sim::kEntitySnapshotFields),
        std::end(og::sim::kEntitySnapshotFields),
        [bit_index](const og::sim::EntitySnapshotFieldDesc& desc) {
            return desc.bit_index == bit_index;
        });
    if (field == std::end(og::sim::kEntitySnapshotFields))
        return;

    std::memcpy(reinterpret_cast<std::uint8_t*>(&destination) + field->snap_offset,
                reinterpret_cast<const std::uint8_t*>(&source) + field->snap_offset,
                field->size);
}

og::sim::EntitySnapshot normalize_delta_entity_for_comparison(
    const og::sim::EntitySnapshot& entity)
{
    og::sim::EntitySnapshot normalized;
    normalized.dirty_mask[0] = entity.dirty_mask[0];
    normalized.dirty_mask[1] = entity.dirty_mask[1];
    normalized.guy_id = entity.guy_id;
    normalized.entity_id = entity.entity_id;

    for (std::uint8_t bit = 0; bit < og::dirty::FIELD_COUNT; ++bit)
    {
        if (!dirty_mask_has_bit(normalized.dirty_mask, bit))
            continue;
        copy_delta_entity_field(normalized, entity, bit);
    }

    return normalized;
}

void normalize_delta_snapshot_for_comparison(og::sim::WorldSnapshot& snapshot)
{
    const auto normalize = [](std::vector<og::sim::EntitySnapshot>& entities) {
        for (auto& entity : entities)
            entity = normalize_delta_entity_for_comparison(entity);
    };

    normalize(snapshot.oblist);
    normalize(snapshot.fxlist);
    normalize(snapshot.weaplist);
}

[[noreturn]] void throw_validation_failure(std::string_view label,
                                           const TransportValidationFailure& failure)
{
    throw std::runtime_error(std::format(
        "{} round-trip mismatch at {}: expected {}, got {}",
        label,
        failure.field,
        failure.expected_value,
        failure.actual_value));
}

std::shared_ptr<og::sim::WorldSnapshot> validate_snapshot_roundtrip(
    const std::shared_ptr<og::sim::WorldSnapshot>& snapshot,
    bool is_delta)
{
    const std::vector<std::uint8_t> bytes =
        is_delta ? og::sim::serialize_delta(*snapshot)
                 : og::sim::serialize_snapshot(*snapshot);
    og::sim::WorldSnapshot decoded =
        is_delta ? og::sim::deserialize_delta(bytes.data(), bytes.size())
                 : og::sim::deserialize_snapshot(bytes.data(), bytes.size());

    const std::optional<og::sim::ReplayVerificationFailure> failure =
        [&]() -> std::optional<og::sim::ReplayVerificationFailure> {
            if (!is_delta)
            {
                return og::sim::find_first_snapshot_difference(snapshot->tick_count,
                                                               *snapshot,
                                                               decoded);
            }

            og::sim::WorldSnapshot expected = *snapshot;
            og::sim::WorldSnapshot actual = decoded;
            normalize_delta_snapshot_for_comparison(expected);
            normalize_delta_snapshot_for_comparison(actual);
            return og::sim::find_first_snapshot_difference(snapshot->tick_count,
                                                           expected,
                                                           actual);
        }();
    if (failure.has_value())
    {
        throw std::runtime_error(std::format(
            "{} round-trip mismatch at tick {} field {}: expected {}, got {}",
            is_delta ? "delta snapshot" : "snapshot",
            failure->tick,
            failure->field,
            failure->expected_value,
            failure->actual_value));
    }

    return std::make_shared<og::sim::WorldSnapshot>(std::move(decoded));
}

std::pair<std::shared_ptr<InputState>, std::uint32_t> validate_input_roundtrip(
    const std::shared_ptr<InputState>& input,
    std::uint32_t tick)
{
    const auto bytes = og::sim::serialize_input(tick, *input);
    const std::optional<og::sim::InputStateMessage> decoded =
        og::sim::deserialize_input_message(bytes);
    if (!decoded.has_value())
    {
        throw std::runtime_error("input round-trip failed to deserialize");
    }

    const std::optional<TransportValidationFailure> failure =
        find_first_input_difference(tick, *input,
                                    decoded->tick, decoded->input);
    if (failure.has_value())
        throw_validation_failure("input", *failure);

    return {
        std::make_shared<InputState>(decoded->input),
        decoded->tick,
    };
}

std::shared_ptr<og::sim::SimEventBatch> validate_event_batch_roundtrip(
    const std::shared_ptr<og::sim::SimEventBatch>& batch,
    bool game_flow)
{
    const std::vector<std::uint8_t> bytes =
        game_flow ? og::sim::serialize_game_flow_event_batch(*batch)
                  : og::sim::serialize_sim_event_batch(*batch);
    og::sim::SimEventBatch decoded =
        game_flow ? og::sim::deserialize_game_flow_event_batch(bytes.data(),
                                                               bytes.size())
                  : og::sim::deserialize_sim_event_batch(bytes.data(),
                                                         bytes.size());

    const std::optional<TransportValidationFailure> failure =
        find_first_event_batch_difference(*batch, decoded);
    if (failure.has_value())
    {
        throw_validation_failure(game_flow ? "game flow event batch"
                                           : "sim event batch",
                                 *failure);
    }

    return std::make_shared<og::sim::SimEventBatch>(std::move(decoded));
}

template <typename Message, typename SerializeFn, typename DeserializeFn>
std::shared_ptr<Message> validate_message_roundtrip(
    const std::shared_ptr<Message>& message,
    SerializeFn&& serialize_fn,
    DeserializeFn&& deserialize_fn,
    std::string_view label)
{
    const std::vector<std::uint8_t> bytes = serialize_fn(*message);
    const std::optional<Message> decoded =
        deserialize_fn(std::span<const std::uint8_t>(bytes.data(), bytes.size()));
    if (!decoded.has_value())
    {
        throw std::runtime_error(std::format(
            "{} round-trip failed to deserialize",
            label));
    }
    if (*decoded != *message)
    {
        throw std::runtime_error(std::format(
            "{} round-trip mismatch",
            label));
    }
    return std::make_shared<Message>(*decoded);
}

std::vector<std::uint8_t> copy_raw_bytes(const std::uint8_t* data, std::size_t len)
{
    if (data == nullptr || len == 0)
        return {};
    return std::vector<std::uint8_t>(data, data + len);
}

} // namespace

namespace og::sim {

std::shared_ptr<InProcessTransport> InProcessTransport::create_server()
{
    return create_server(Options{});
}

std::shared_ptr<InProcessTransport> InProcessTransport::create_server(
    const Options& options)
{
    return std::shared_ptr<InProcessTransport>(
        new InProcessTransport(true, 0, options));
}

InProcessTransport::LinkedPair InProcessTransport::create_linked_pair()
{
    return create_linked_pair(Options{});
}

InProcessTransport::LinkedPair InProcessTransport::create_linked_pair(
    const Options& options)
{
    LinkedPair pair;
    pair.server = create_server(options);
    pair.client = pair.server->create_client_transport();
    pair.peer_id = pair.client->local_peer_id();
    return pair;
}

std::shared_ptr<InProcessTransport> InProcessTransport::create_client_transport()
{
    if (!is_server_side_)
    {
        throw std::runtime_error(
            "InProcessTransport::create_client_transport requires a server transport");
    }

    PeerId assigned_peer_id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        assigned_peer_id = next_peer_id_++;
    }

    Options options;
    options.validate_serialization = validate_serialization_;
    auto client = std::shared_ptr<InProcessTransport>(
        new InProcessTransport(false, assigned_peer_id, options));

    attach_peer(assigned_peer_id, client);
    client->attach_peer(assigned_peer_id, shared_from_this());
    return client;
}

bool InProcessTransport::supports_typed_messages() const noexcept
{
    return true;
}

void InProcessTransport::send(PeerId peer_id,
                              const std::uint8_t* data,
                              std::size_t len)
{
    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    peer->enqueue_raw(peer_id, copy_raw_bytes(data, len));
}

void InProcessTransport::send_snapshot(PeerId peer_id,
                                       std::shared_ptr<WorldSnapshot> snapshot)
{
    if (!snapshot)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage message;
    message.peer_id = peer_id;
    message.kind = TypedReceivedMessageKind::Snapshot;
    message.snapshot = validate_serialization_
        ? validate_snapshot_roundtrip(snapshot, false)
        : std::move(snapshot);
    peer->enqueue_typed(std::move(message));
}

void InProcessTransport::send_delta_snapshot(
    PeerId peer_id,
    std::shared_ptr<WorldSnapshot> snapshot)
{
    if (!snapshot)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage message;
    message.peer_id = peer_id;
    message.kind = TypedReceivedMessageKind::DeltaSnapshot;
    message.snapshot = validate_serialization_
        ? validate_snapshot_roundtrip(snapshot, true)
        : std::move(snapshot);
    peer->enqueue_typed(std::move(message));
}

void InProcessTransport::send_input(PeerId peer_id,
                                    std::shared_ptr<InputState> input,
                                    std::uint32_t tick)
{
    if (!input)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage message;
    message.peer_id = peer_id;
    message.kind = TypedReceivedMessageKind::Input;
    if (validate_serialization_)
    {
        auto [validated_input, validated_tick] =
            validate_input_roundtrip(input, tick);
        message.input = std::move(validated_input);
        message.tick = validated_tick;
    }
    else
    {
        message.input = std::move(input);
        message.tick = tick;
    }
    peer->enqueue_typed(std::move(message));
}

void InProcessTransport::send_sim_event_batch(
    PeerId peer_id,
    std::shared_ptr<SimEventBatch> batch)
{
    if (!batch)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage message;
    message.peer_id = peer_id;
    message.kind = TypedReceivedMessageKind::SimEventBatch;
    message.event_batch = validate_serialization_
        ? validate_event_batch_roundtrip(batch, false)
        : std::move(batch);
    peer->enqueue_typed(std::move(message));
}

void InProcessTransport::send_game_flow_event_batch(
    PeerId peer_id,
    std::shared_ptr<SimEventBatch> batch)
{
    if (!batch)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage message;
    message.peer_id = peer_id;
    message.kind = TypedReceivedMessageKind::GameFlowEventBatch;
    message.event_batch = validate_serialization_
        ? validate_event_batch_roundtrip(batch, true)
        : std::move(batch);
    peer->enqueue_typed(std::move(message));
}

void InProcessTransport::send_initial_setup(
    PeerId peer_id,
    std::shared_ptr<InitialSetupMessage> message)
{
    if (!message)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = TypedReceivedMessageKind::InitialSetup;
    typed_message.initial_setup = validate_serialization_
        ? validate_message_roundtrip(
              message,
              serialize_initial_setup_message,
              deserialize_initial_setup_message,
              "initial setup")
        : std::move(message);
    peer->enqueue_typed(std::move(typed_message));
}

void InProcessTransport::send_client_ready(
    PeerId peer_id,
    std::shared_ptr<ClientReadyMessage> message)
{
    if (!message)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = TypedReceivedMessageKind::ClientReady;
    typed_message.client_ready = validate_serialization_
        ? validate_message_roundtrip(
              message,
              serialize_client_ready_message,
              deserialize_client_ready_message,
              "client ready")
        : std::move(message);
    peer->enqueue_typed(std::move(typed_message));
}

void InProcessTransport::send_keyframe_request(
    PeerId peer_id,
    std::shared_ptr<KeyframeRequestMessage> message)
{
    if (!message)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = TypedReceivedMessageKind::KeyframeRequest;
    typed_message.keyframe_request = validate_serialization_
        ? validate_message_roundtrip(
              message,
              serialize_keyframe_request_message,
              deserialize_keyframe_request_message,
              "keyframe request")
        : std::move(message);
    peer->enqueue_typed(std::move(typed_message));
}

void InProcessTransport::send_exit_prompt_broadcast(
    PeerId peer_id,
    std::shared_ptr<ExitPromptBroadcastMessage> message)
{
    if (!message)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = TypedReceivedMessageKind::ExitPromptBroadcast;
    typed_message.exit_prompt_broadcast = validate_serialization_
        ? validate_message_roundtrip(
              message,
              serialize_exit_prompt_broadcast_message,
              deserialize_exit_prompt_broadcast_message,
              "exit prompt broadcast")
        : std::move(message);
    peer->enqueue_typed(std::move(typed_message));
}

void InProcessTransport::send_exit_prompt_response(
    PeerId peer_id,
    std::shared_ptr<ExitPromptResponseMessage> message)
{
    if (!message)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = TypedReceivedMessageKind::ExitPromptResponse;
    typed_message.exit_prompt_response = validate_serialization_
        ? validate_message_roundtrip(
              message,
              serialize_exit_prompt_response_message,
              deserialize_exit_prompt_response_message,
              "exit prompt response")
        : std::move(message);
    peer->enqueue_typed(std::move(typed_message));
}

void InProcessTransport::send_pause_broadcast(
    PeerId peer_id,
    std::shared_ptr<PauseBroadcastMessage> message)
{
    if (!message)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = TypedReceivedMessageKind::PauseBroadcast;
    typed_message.pause_broadcast = validate_serialization_
        ? validate_message_roundtrip(
              message,
              serialize_pause_broadcast_message,
              deserialize_pause_broadcast_message,
              "pause broadcast")
        : std::move(message);
    peer->enqueue_typed(std::move(typed_message));
}

void InProcessTransport::send_pause_response(
    PeerId peer_id,
    std::shared_ptr<PauseResponseMessage> message)
{
    if (!message)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = TypedReceivedMessageKind::PauseResponse;
    typed_message.pause_response = validate_serialization_
        ? validate_message_roundtrip(
              message,
              serialize_pause_response_message,
              deserialize_pause_response_message,
              "pause response")
        : std::move(message);
    peer->enqueue_typed(std::move(typed_message));
}

void InProcessTransport::send_control_change(
    PeerId peer_id,
    std::shared_ptr<ControlChangeMessage> message)
{
    if (!message)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = TypedReceivedMessageKind::ControlChange;
    typed_message.control_change = validate_serialization_
        ? validate_message_roundtrip(
              message,
              serialize_control_change_message,
              deserialize_control_change_message,
              "control change")
        : std::move(message);
    peer->enqueue_typed(std::move(typed_message));
}

void InProcessTransport::send_snapshot_hash_check(
    PeerId peer_id,
    std::shared_ptr<SnapshotHashCheckMessage> message)
{
    if (!message)
        return;

    const std::shared_ptr<InProcessTransport> peer = resolve_peer(peer_id);
    TypedReceivedMessage typed_message;
    typed_message.peer_id = peer_id;
    typed_message.kind = TypedReceivedMessageKind::SnapshotHashCheck;
    typed_message.snapshot_hash_check = validate_serialization_
        ? validate_message_roundtrip(
              message,
              serialize_snapshot_hash_check_message,
              deserialize_snapshot_hash_check_message,
              "snapshot hash check")
        : std::move(message);
    peer->enqueue_typed(std::move(typed_message));
}

std::vector<ReceivedMessage> InProcessTransport::poll()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ReceivedMessage> drained = std::move(raw_messages_);
    raw_messages_.clear();
    return drained;
}

std::vector<TypedReceivedMessage> InProcessTransport::poll_typed()
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TypedReceivedMessage> drained = std::move(typed_messages_);
    typed_messages_.clear();
    return drained;
}

void InProcessTransport::accept_connections()
{
    std::lock_guard<std::mutex> lock(mutex_);
    accepting_connections_ = true;
}

void InProcessTransport::disconnect(PeerId peer_id)
{
    std::shared_ptr<InProcessTransport> peer;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = peers_.find(peer_id);
        if (it == peers_.end())
            return;
        peer = it->second.lock();
        peers_.erase(it);
    }

    if (peer)
        peer->detach_peer(peer_id);
}

std::vector<PeerId> InProcessTransport::connected_peers() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PeerId> peers;
    peers.reserve(peers_.size());
    for (const auto& [peer_id, peer] : peers_)
    {
        if (!peer.expired())
            peers.push_back(peer_id);
    }
    std::sort(peers.begin(), peers.end());
    return peers;
}

InProcessTransport::InProcessTransport(bool is_server_side,
                                       PeerId local_peer_id,
                                       const Options& options)
    : is_server_side_(is_server_side)
    , local_peer_id_(local_peer_id)
    , validate_serialization_(options.validate_serialization)
{
}

void InProcessTransport::attach_peer(
    PeerId peer_id,
    const std::shared_ptr<InProcessTransport>& peer_transport)
{
    std::lock_guard<std::mutex> lock(mutex_);
    peers_[peer_id] = peer_transport;
}

void InProcessTransport::detach_peer(PeerId peer_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    peers_.erase(peer_id);
}

std::shared_ptr<InProcessTransport> InProcessTransport::resolve_peer(
    PeerId peer_id) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = peers_.find(peer_id);
    if (it == peers_.end())
    {
        throw std::runtime_error(
            std::format("InProcessTransport peer {} is not connected", peer_id));
    }

    const std::shared_ptr<InProcessTransport> peer = it->second.lock();
    if (!peer)
    {
        throw std::runtime_error(
            std::format("InProcessTransport peer {} has expired", peer_id));
    }
    return peer;
}

void InProcessTransport::enqueue_raw(PeerId peer_id, std::vector<std::uint8_t> bytes)
{
    std::lock_guard<std::mutex> lock(mutex_);
    raw_messages_.push_back({peer_id, std::move(bytes)});
}

void InProcessTransport::enqueue_typed(TypedReceivedMessage message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    typed_messages_.push_back(std::move(message));
}

} // namespace og::sim
