#include <openglad/gameplay/game_client.h>

#include <openglad/core/util.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_constants.h>

#include <stdexcept>
#include <utility>

namespace {

void clear_transport_only_snapshot_state(og::sim::WorldSnapshot& snapshot)
{
    snapshot.removed_entity_ids.clear();
}

void clear_transport_only_world_state(GameWorld& world)
{
    world.clear_removed_entity_ids();
    world.clear_grid_dirty_tiles();
}

void apply_initial_setup_to_world(GameWorld& world,
                                  const og::sim::InitialSetupMessage& message)
{
    world.id = message.level_id;
    world.title = message.level_title;
    world.type = static_cast<char>(message.level_type);
    world.par_value = message.par_value;
    world.time_bonus_limit = message.time_bonus_limit;
    world.difficulty = message.difficulty;
    world.pixmaxx = message.pixmaxx;
    world.pixmaxy = message.pixmaxy;
    world.my_team = message.my_team;
    world.allied_mode = message.allied_mode;
    world.current_scenario = message.current_scenario;
    world.completed_levels.clear();
    for (const std::int32_t level_id : message.completed_levels)
        world.completed_levels.insert(level_id);
}

std::vector<og::sim::TypedReceivedMessage> poll_client_messages(
    og::sim::ITransport& transport)
{
    if (transport.supports_typed_messages())
        return transport.poll_typed();

    std::vector<og::sim::TypedReceivedMessage> typed_messages;
    for (const auto& message : transport.poll())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope))
        {
            throw std::runtime_error(
                "GameClient received malformed transport header");
        }

        og::sim::TypedReceivedMessage typed_message;
        typed_message.peer_id = message.peer_id;
        switch (envelope.message_type)
        {
        case og::sim::kSnapshotMessageType:
            typed_message.kind = og::sim::TypedReceivedMessageKind::Snapshot;
            typed_message.snapshot = std::make_shared<og::sim::WorldSnapshot>(
                og::sim::deserialize_snapshot(message.data.data(),
                                              message.data.size()));
            break;

        case og::sim::kDeltaSnapshotMessageType:
            typed_message.kind = og::sim::TypedReceivedMessageKind::DeltaSnapshot;
            typed_message.snapshot = std::make_shared<og::sim::WorldSnapshot>(
                og::sim::deserialize_delta(message.data.data(),
                                           message.data.size()));
            break;

        case og::sim::kSimEventBatchMessageType:
            typed_message.kind = og::sim::TypedReceivedMessageKind::SimEventBatch;
            typed_message.event_batch = std::make_shared<og::sim::SimEventBatch>(
                og::sim::deserialize_sim_event_batch(message.data.data(),
                                                     message.data.size()));
            break;

        case og::sim::kGameFlowEventBatchMessageType:
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::GameFlowEventBatch;
            typed_message.event_batch = std::make_shared<og::sim::SimEventBatch>(
                og::sim::deserialize_game_flow_event_batch(message.data.data(),
                                                           message.data.size()));
            break;

        case og::sim::kInputMessageType:
        {
            const std::optional<og::sim::InputStateMessage> decoded =
                og::sim::deserialize_input_message(message.data);
            if (!decoded.has_value())
            {
                throw std::runtime_error(
                    "GameClient failed to deserialize input message");
            }
            typed_message.kind = og::sim::TypedReceivedMessageKind::Input;
            typed_message.input = std::make_shared<InputState>(decoded->input);
            typed_message.tick = decoded->tick;
            break;
        }

        case og::sim::kInitialSetupMessageType:
        {
            const auto decoded =
                og::sim::deserialize_initial_setup_message(message.data);
            if (!decoded.has_value())
            {
                throw std::runtime_error(
                    "GameClient failed to deserialize initial setup");
            }
            typed_message.kind = og::sim::TypedReceivedMessageKind::InitialSetup;
            typed_message.initial_setup =
                std::make_shared<og::sim::InitialSetupMessage>(*decoded);
            break;
        }

        case og::sim::kExitPromptBroadcastMessageType:
        {
            const auto decoded =
                og::sim::deserialize_exit_prompt_broadcast_message(message.data);
            if (!decoded.has_value())
            {
                throw std::runtime_error(
                    "GameClient failed to deserialize exit prompt broadcast");
            }
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::ExitPromptBroadcast;
            typed_message.exit_prompt_broadcast =
                std::make_shared<og::sim::ExitPromptBroadcastMessage>(*decoded);
            break;
        }

        case og::sim::kPauseBroadcastMessageType:
        {
            const auto decoded =
                og::sim::deserialize_pause_broadcast_message(message.data);
            if (!decoded.has_value())
            {
                throw std::runtime_error(
                    "GameClient failed to deserialize pause broadcast");
            }
            typed_message.kind =
                og::sim::TypedReceivedMessageKind::PauseBroadcast;
            typed_message.pause_broadcast =
                std::make_shared<og::sim::PauseBroadcastMessage>(*decoded);
            break;
        }

        case og::sim::kControlChangeMessageType:
        {
            const auto decoded =
                og::sim::deserialize_control_change_message(message.data);
            if (!decoded.has_value())
            {
                throw std::runtime_error(
                    "GameClient failed to deserialize control change");
            }
            typed_message.kind = og::sim::TypedReceivedMessageKind::ControlChange;
            typed_message.control_change =
                std::make_shared<og::sim::ControlChangeMessage>(*decoded);
            break;
        }

        default:
            continue;
        }

        typed_messages.push_back(std::move(typed_message));
    }

    return typed_messages;
}

} // namespace

namespace og::sim {

GameClient::GameClient(ITransport& transport,
                       PeerId server_peer_id,
                       GameWorld* world)
    : transport_(transport)
    , server_peer_id_(server_peer_id)
    , world_(world)
{
}

void GameClient::set_control_mapping_callback(
    std::function<void(const std::array<std::uint32_t, MAX_PLAYERS>&,
                       GameWorld*)> callback)
{
    control_mapping_callback_ = std::move(callback);
}

void GameClient::set_initial_setup_callback(
    std::function<void(const InitialSetupMessage&, bool)> callback)
{
    initial_setup_callback_ = std::move(callback);
}

void GameClient::set_sim_event_batch_callback(
    std::function<void(const SimEventBatch&)> callback)
{
    sim_event_batch_callback_ = std::move(callback);
}

void GameClient::set_game_flow_event_batch_callback(
    std::function<void(const SimEventBatch&)> callback)
{
    game_flow_event_batch_callback_ = std::move(callback);
}

void GameClient::set_exit_prompt_callback(
    std::function<void(const ExitPromptBroadcastMessage&)> callback)
{
    exit_prompt_callback_ = std::move(callback);
}

void GameClient::set_pause_broadcast_callback(
    std::function<void(const PauseBroadcastMessage&)> callback)
{
    pause_broadcast_callback_ = std::move(callback);
}

void GameClient::set_palette_sync_callback(
    std::function<void(std::uint8_t)> callback)
{
    palette_sync_callback_ = std::move(callback);
}

void GameClient::set_message_processing_break_callback(
    std::function<bool()> callback)
{
    message_processing_break_callback_ = std::move(callback);
}

void GameClient::send_input(const InputState& input, std::uint32_t tick)
{
    transport_.send_input(server_peer_id_,
                          std::make_shared<InputState>(input),
                          tick);
}

void GameClient::send_client_ready()
{
    ClientReadyMessage message;
    message.last_applied_tick =
        baseline_.has_value() ? baseline_->tick_count : last_seen_server_tick_;
    transport_.send_client_ready(
        server_peer_id_,
        std::make_shared<ClientReadyMessage>(message));
    client_ready_sent_ = true;
    ++client_ready_count_;
}

void GameClient::send_keyframe_request(std::uint32_t last_seen_tick)
{
    KeyframeRequestMessage message;
    message.last_seen_tick =
        last_seen_tick != 0 ? last_seen_tick : last_seen_server_tick_;
    transport_.send_keyframe_request(
        server_peer_id_,
        std::make_shared<KeyframeRequestMessage>(message));
    waiting_for_keyframe_ = true;
    ++keyframe_request_count_;
}

void GameClient::send_exit_prompt_response(bool accepted)
{
    ExitPromptResponseMessage message;
    message.accepted = accepted;
    transport_.send_exit_prompt_response(
        server_peer_id_,
        std::make_shared<ExitPromptResponseMessage>(message));
}

void GameClient::send_pause_request()
{
    transport_.send_pause_broadcast(
        server_peer_id_,
        std::make_shared<PauseBroadcastMessage>());
}

void GameClient::send_pause_response()
{
    PauseResponseMessage message;
    message.resume = true;
    transport_.send_pause_response(
        server_peer_id_,
        std::make_shared<PauseResponseMessage>(message));
}

std::uint32_t GameClient::compute_local_snapshot_hash() const
{
    if (world_ != nullptr)
    {
        return compute_snapshot_hash(peek_keyframe_snapshot(*world_));
    }
    if (baseline_.has_value())
    {
        return compute_snapshot_hash(*baseline_);
    }
    return 0;
}

void GameClient::send_snapshot_hash_check()
{
    if (!baseline_.has_value() && world_ == nullptr)
        return;

    SnapshotHashCheckMessage message;
    message.tick = baseline_.has_value() ? baseline_->tick_count : last_seen_server_tick_;
    message.snapshot_hash = compute_local_snapshot_hash();
    transport_.send_snapshot_hash_check(
        server_peer_id_,
        std::make_shared<SnapshotHashCheckMessage>(message));
    ++snapshot_hash_check_count_;
}

void GameClient::maybe_send_client_ready()
{
    if (!client_ready_sent_ && initial_setup_.has_value() && baseline_.has_value())
        send_client_ready();
}

void GameClient::maybe_send_snapshot_hash_check(bool force)
{
    if (!baseline_.has_value())
        return;

    if (!force &&
        (baseline_->tick_count % KEYFRAME_INTERVAL_TICKS) != 0)
    {
        return;
    }

    send_snapshot_hash_check();
}

void GameClient::notify_control_mapping_changed()
{
    if (control_mapping_callback_)
        control_mapping_callback_(controlled_entity_ids_, world_);
}

void GameClient::notify_initial_setup(const InitialSetupMessage& message,
                                      bool is_level_transition)
{
    if (initial_setup_callback_)
        initial_setup_callback_(message, is_level_transition);
}

void GameClient::notify_sim_event_batch(const SimEventBatch& batch)
{
    if (sim_event_batch_callback_)
        sim_event_batch_callback_(batch);
}

void GameClient::notify_game_flow_event_batch(const SimEventBatch& batch)
{
    if (game_flow_event_batch_callback_)
        game_flow_event_batch_callback_(batch);
}

void GameClient::notify_exit_prompt(const ExitPromptBroadcastMessage& message)
{
    if (exit_prompt_callback_)
        exit_prompt_callback_(message);
}

void GameClient::notify_pause_broadcast(const PauseBroadcastMessage& message)
{
    if (pause_broadcast_callback_)
        pause_broadcast_callback_(message);
}

void GameClient::notify_palette_sync(std::uint8_t palette_id)
{
    if (palette_sync_callback_)
        palette_sync_callback_(palette_id);
}

void GameClient::apply_initial_setup(const InitialSetupMessage& message)
{
    const bool is_level_transition = baseline_.has_value();
    initial_setup_ = message;
    controlled_entity_ids_ = message.controlled_entity_ids;
    initial_setup_guys_.clear();
    for (const auto& guy : message.guys)
        initial_setup_guys_[guy.guy_id] = guy;

    if (world_ != nullptr)
        apply_initial_setup_to_world(*world_, message);

    baseline_.reset();
    last_seen_server_tick_ = 0;
    waiting_for_keyframe_ = false;
    client_ready_sent_ = false;
    has_sim_event_sequence_ = false;
    has_game_flow_event_sequence_ = false;
    last_exit_prompt_.reset();
    last_pause_broadcast_.reset();
    notify_initial_setup(message, is_level_transition);
    notify_control_mapping_changed();
}

void GameClient::apply_full_snapshot(const WorldSnapshot& snapshot)
{
    baseline_ = snapshot;
    if (world_ != nullptr)
    {
        apply_snapshot(*world_, *baseline_);
        clear_transport_only_world_state(*world_);
    }
    clear_transport_only_snapshot_state(*baseline_);
    last_seen_server_tick_ = baseline_->tick_count;
    notify_control_mapping_changed();
    notify_palette_sync(baseline_->current_palette_id);
    waiting_for_keyframe_ = false;
    maybe_send_client_ready();
    maybe_send_snapshot_hash_check(true);
}

void GameClient::apply_delta_snapshot(const WorldSnapshot& snapshot)
{
    if (waiting_for_keyframe_)
        return;

    if (!baseline_.has_value())
    {
        send_keyframe_request();
        return;
    }

    const std::uint32_t expected_tick =
        baseline_.has_value() ? baseline_->tick_count + 1 : 0;
    if (expected_tick != 0 && snapshot.tick_count != expected_tick)
    {
        note_event_batch_gap(expected_tick,
                             snapshot.tick_count,
                             "delta gap");
        send_keyframe_request(last_seen_server_tick_);
        return;
    }

    apply_delta(*baseline_, snapshot);
    if (world_ != nullptr)
    {
        apply_snapshot(*world_, *baseline_);
        clear_transport_only_world_state(*world_);
    }
    clear_transport_only_snapshot_state(*baseline_);
    last_seen_server_tick_ = baseline_->tick_count;
    notify_control_mapping_changed();
    notify_palette_sync(baseline_->current_palette_id);
    maybe_send_snapshot_hash_check(false);
}

void GameClient::note_event_batch_gap(std::uint32_t expected,
                                      std::uint32_t actual,
                                      const char* label) const
{
    LogError("game_client_{} expected={} actual={}\n",
             label,
             expected,
             actual);
}

void GameClient::poll_messages()
{
    last_polled_messages_ = poll_client_messages(transport_);
    sim_event_batches_.clear();
    game_flow_event_batches_.clear();

    for (const auto& message : last_polled_messages_)
    {
        if (message.peer_id != server_peer_id_)
            continue;

        switch (message.kind)
        {
        case TypedReceivedMessageKind::Snapshot:
            if (message.snapshot)
                apply_full_snapshot(*message.snapshot);
            break;

        case TypedReceivedMessageKind::DeltaSnapshot:
            if (message.snapshot)
                apply_delta_snapshot(*message.snapshot);
            break;

        case TypedReceivedMessageKind::SimEventBatch:
            if (message.event_batch)
            {
                if (has_sim_event_sequence_ &&
                    message.event_batch->sequence != last_sim_event_sequence_ + 1)
                {
                    note_event_batch_gap(last_sim_event_sequence_ + 1,
                                         message.event_batch->sequence,
                                         "sim_event_gap");
                }
                has_sim_event_sequence_ = true;
                last_sim_event_sequence_ = message.event_batch->sequence;
                sim_event_batches_.push_back(*message.event_batch);
                notify_sim_event_batch(*message.event_batch);
            }
            break;

        case TypedReceivedMessageKind::GameFlowEventBatch:
            if (message.event_batch)
            {
                if (has_game_flow_event_sequence_ &&
                    message.event_batch->sequence !=
                        last_game_flow_event_sequence_ + 1)
                {
                    note_event_batch_gap(last_game_flow_event_sequence_ + 1,
                                         message.event_batch->sequence,
                                         "game_flow_gap");
                }
                has_game_flow_event_sequence_ = true;
                last_game_flow_event_sequence_ = message.event_batch->sequence;
                game_flow_event_batches_.push_back(*message.event_batch);
                notify_game_flow_event_batch(*message.event_batch);
            }
            break;

        case TypedReceivedMessageKind::InitialSetup:
            if (message.initial_setup)
                apply_initial_setup(*message.initial_setup);
            break;

        case TypedReceivedMessageKind::ExitPromptBroadcast:
            if (message.exit_prompt_broadcast)
            {
                last_exit_prompt_ = *message.exit_prompt_broadcast;
                notify_exit_prompt(*message.exit_prompt_broadcast);
            }
            break;

        case TypedReceivedMessageKind::PauseBroadcast:
            if (message.pause_broadcast)
            {
                last_pause_broadcast_ = *message.pause_broadcast;
                notify_pause_broadcast(*message.pause_broadcast);
            }
            break;

        case TypedReceivedMessageKind::ControlChange:
            if (message.control_change &&
                message.control_change->player_index < controlled_entity_ids_.size())
            {
                controlled_entity_ids_[message.control_change->player_index] =
                    message.control_change->entity_id;
                notify_control_mapping_changed();
            }
            break;

        case TypedReceivedMessageKind::Input:
        case TypedReceivedMessageKind::ClientReady:
        case TypedReceivedMessageKind::KeyframeRequest:
        case TypedReceivedMessageKind::ExitPromptResponse:
        case TypedReceivedMessageKind::PauseResponse:
        case TypedReceivedMessageKind::SnapshotHashCheck:
            break;
        }

        if (message_processing_break_callback_ &&
            message_processing_break_callback_())
        {
            break;
        }
    }
}

} // namespace og::sim
