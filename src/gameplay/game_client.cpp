#include <openglad/gameplay/game_client.h>

#include <openglad/core/runtime_trace.h>
#include <openglad/core/test_trace.h>
#include <openglad/core/util.h>
#include <openglad/gameplay/game_world.h>
#include <openglad/gameplay/input_state_net.h>
#include <openglad/gameplay/net_constants.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <stdexcept>
#include <utility>

namespace {

constexpr auto kHeartbeatInterval = std::chrono::seconds(2);

void clear_transport_only_snapshot_state(og::sim::WorldSnapshot& snapshot)
{
    snapshot.removed_entity_ids.clear();
}

void clear_transport_only_world_state(GameWorld& world)
{
    world.clear_removed_entity_ids();
    world.clear_grid_dirty_tiles();
}

float clamp_alpha(float alpha) noexcept
{
    return std::clamp(alpha, 0.0f, 1.0f);
}

float lerp(float start, float end, float alpha) noexcept
{
    return start + (end - start) * alpha;
}

struct ClientPollResult {
    std::vector<og::sim::TypedReceivedMessage> messages;
    bool malformed_server_message = false;
};

ClientPollResult poll_client_messages(
    og::sim::ITransport& transport)
{
    ClientPollResult result;
    if (transport.supports_typed_messages())
    {
        for (og::sim::TypedReceivedMessage& message : transport.poll_typed())
        {
            if (message.kind == og::sim::TypedReceivedMessageKind::Malformed)
            {
                result.malformed_server_message = true;
                break;
            }

            result.messages.push_back(std::move(message));
        }
        return result;
    }

    for (const auto& message : transport.poll())
    {
        og::sim::TransportEnvelope envelope;
        if (!og::sim::decode_transport_envelope(message.data, envelope))
        {
            result.malformed_server_message = true;
            break;
        }

        og::sim::TypedReceivedMessage typed_message;
        typed_message.peer_id = message.peer_id;
        try
        {
            switch (envelope.message_type)
            {
            case og::sim::kSnapshotMessageType:
                typed_message.kind = og::sim::TypedReceivedMessageKind::Snapshot;
                typed_message.snapshot = std::make_shared<og::sim::WorldSnapshot>(
                    og::sim::deserialize_snapshot(message.data));
                break;

            case og::sim::kDeltaSnapshotMessageType:
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::DeltaSnapshot;
                typed_message.snapshot = std::make_shared<og::sim::WorldSnapshot>(
                    og::sim::deserialize_delta(message.data));
                break;

            case og::sim::kSimEventBatchMessageType:
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::SimEventBatch;
                typed_message.event_batch =
                    std::make_shared<og::sim::SimEventBatch>(
                        og::sim::deserialize_sim_event_batch(message.data));
                break;

            case og::sim::kGameFlowEventBatchMessageType:
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::GameFlowEventBatch;
                typed_message.event_batch =
                    std::make_shared<og::sim::SimEventBatch>(
                        og::sim::deserialize_game_flow_event_batch(
                            message.data));
                break;

            case og::sim::kLobbyMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_lobby_message(message.data);
                if (!decoded.has_value())
                {
                    result.malformed_server_message = true;
                    break;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::LobbyMessage;
                typed_message.lobby_message =
                    std::make_shared<og::sim::LobbyMessage>(*decoded);
                break;
            }

            case og::sim::kLobbyStateMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_lobby_state_message(message.data);
                if (!decoded.has_value())
                {
                    result.malformed_server_message = true;
                    break;
                }
                typed_message.kind = og::sim::TypedReceivedMessageKind::LobbyState;
                typed_message.lobby_state =
                    std::make_shared<og::sim::LobbyState>(*decoded);
                break;
            }

            case og::sim::kInputMessageType:
            {
                const std::optional<og::sim::InputStateMessage> decoded =
                    og::sim::deserialize_input_message(message.data);
                if (!decoded.has_value())
                {
                    result.malformed_server_message = true;
                    break;
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
                    result.malformed_server_message = true;
                    break;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::InitialSetup;
                typed_message.initial_setup =
                    std::make_shared<og::sim::InitialSetupMessage>(*decoded);
                break;
            }

            case og::sim::kHelloMessageType:
            {
                const auto decoded = og::sim::deserialize_hello_message(message.data);
                if (!decoded.has_value())
                {
                    result.malformed_server_message = true;
                    break;
                }
                typed_message.kind = og::sim::TypedReceivedMessageKind::Hello;
                typed_message.hello =
                    std::make_shared<og::sim::HelloMessage>(*decoded);
                break;
            }

            case og::sim::kHeartbeatMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_heartbeat_message(message.data);
                if (!decoded.has_value())
                {
                    result.malformed_server_message = true;
                    break;
                }
                typed_message.kind = og::sim::TypedReceivedMessageKind::Heartbeat;
                typed_message.heartbeat =
                    std::make_shared<og::sim::HeartbeatMessage>(*decoded);
                break;
            }

            case og::sim::kExitPromptBroadcastMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_exit_prompt_broadcast_message(message.data);
                if (!decoded.has_value())
                {
                    result.malformed_server_message = true;
                    break;
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
                    result.malformed_server_message = true;
                    break;
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
                    result.malformed_server_message = true;
                    break;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::ControlChange;
                typed_message.control_change =
                    std::make_shared<og::sim::ControlChangeMessage>(*decoded);
                break;
            }

            case og::sim::kPackManifestMessageType:
            {
                // Pack transfers are handled by the lobby-phase picker
                // clients; chunks that trail into the gameplay stream are
                // legal traffic the dispatcher ignores — never grounds for
                // disconnecting the server.
                const auto decoded =
                    og::sim::deserialize_pack_manifest_message(message.data);
                if (!decoded.has_value())
                {
                    result.malformed_server_message = true;
                    break;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::PackManifest;
                typed_message.pack_manifest =
                    std::make_shared<og::sim::PackManifestMessage>(*decoded);
                break;
            }

            case og::sim::kPackFileChunkMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_pack_file_chunk_message(message.data);
                if (!decoded.has_value())
                {
                    result.malformed_server_message = true;
                    break;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::PackFileChunk;
                typed_message.pack_file_chunk =
                    std::make_shared<og::sim::PackFileChunkMessage>(*decoded);
                break;
            }

            case og::sim::kPackTransferDoneMessageType:
            {
                const auto decoded =
                    og::sim::deserialize_pack_transfer_done_message(
                        message.data);
                if (!decoded.has_value())
                {
                    result.malformed_server_message = true;
                    break;
                }
                typed_message.kind =
                    og::sim::TypedReceivedMessageKind::PackTransferDone;
                typed_message.pack_transfer_done =
                    std::make_shared<og::sim::PackTransferDoneMessage>(
                        *decoded);
                break;
            }

            default:
                result.malformed_server_message = true;
                break;
            }
        }
        catch (const std::exception&)
        {
            result.malformed_server_message = true;
        }

        if (result.malformed_server_message)
            break;

        result.messages.push_back(std::move(typed_message));
    }

    return result;
}

} // namespace

namespace og::sim {

// Promoted out of this file's anonymous namespace for the staged-lobby
// preview mirror (C9): the joiner's mirror and the gameplay client's own
// setup apply must never fork their level-metadata semantics (the same rule
// that promoted collect_initial_setup_guys on the server side).
void apply_initial_setup_to_world(GameWorld& world,
                                  const InitialSetupMessage& message)
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
    world.respawn_mode = message.respawn_mode;
    world.generator_rate = message.generator_rate;
    world.completed_levels.clear();
    for (const std::int32_t level_id : message.completed_levels)
        world.completed_levels.insert(level_id);
}

void GameClient::poll_messages()
{
    poll_messages_impl(render_interpolation_alpha(1.0f), INT_MAX);
}

void GameClient::poll_messages(float current_render_alpha)
{
    poll_messages_impl(clamp_alpha(current_render_alpha), INT_MAX);
}

void GameClient::poll_messages(float current_render_alpha, int max_messages)
{
    poll_messages_impl(clamp_alpha(current_render_alpha), max_messages);
}

float GameClient::render_interpolation_alpha(float speed_factor) const
{
    last_render_speed_factor_ = speed_factor;

    if (!last_snapshot_receive_time_.has_value())
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_client", "render_alpha_without_snapshot"));
        return 1.0f;
    }

    const short timer_wait =
        baseline_.has_value() ? baseline_->timer_wait : og::sim::DEFAULT_TIMER_WAIT;
    const float tick_interval_ms =
        og::core::rounded_render_tick_interval_ms(timer_wait, speed_factor);
    if (tick_interval_ms <= 0.0f)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_client", "render_alpha_immediate"));
        return 1.0f;
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::duration<float, std::milli>>(
            InterpolationClock::now() - *last_snapshot_receive_time_);
    const float alpha = clamp_alpha(elapsed.count() / tick_interval_ms);
    og::runtime::RuntimeTraceRecord trace =
        og::runtime::make_runtime_trace_record(
            "game_client", "render_alpha_computed");
    trace.interpolation_alpha = alpha;
    og::runtime::emit_runtime_trace(std::move(trace));
    return alpha;
}

std::optional<RenderInterpolationPosition> GameClient::render_position(
    std::uint32_t entity_id,
    float alpha) const
{
    const auto it = render_interpolation_.find(entity_id);
    if (it == render_interpolation_.end())
        return std::nullopt;

    return interpolate_position(it->second, alpha);
}

void GameClient::testing_set_render_interpolation_elapsed_ms(float elapsed_ms)
{
    last_snapshot_receive_time_ =
        InterpolationClock::now() -
        std::chrono::duration_cast<InterpolationClock::duration>(
            std::chrono::duration<float, std::milli>(
                std::max(elapsed_ms, 0.0f)));
}

void GameClient::testing_set_last_outbound_activity_elapsed_ms(float elapsed_ms)
{
    last_outbound_activity_time_ =
        InterpolationClock::now() -
        std::chrono::duration_cast<InterpolationClock::duration>(
            std::chrono::duration<float, std::milli>(
                std::max(elapsed_ms, 0.0f)));
}

void GameClient::testing_set_transport_disconnect_elapsed_ms(float elapsed_ms)
{
    transport_ever_connected_ = true;
    connection_lost_notified_ = false;
    transport_disconnect_time_ =
        InterpolationClock::now() -
        std::chrono::duration_cast<InterpolationClock::duration>(
            std::chrono::duration<float, std::milli>(
                std::max(elapsed_ms, 0.0f)));
}

GameClient::GameClient(ITransport& transport,
                       PeerId server_peer_id,
                       GameWorld* world)
    : transport_(transport)
    , server_peer_id_(server_peer_id)
    , world_(world)
{
}

void GameClient::set_control_mapping_callback(
    std::function<void(const ControlledEntityIds&, GameWorld*)> callback)
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

void GameClient::set_connection_lost_callback(std::function<void()> callback)
{
    connection_lost_callback_ = std::move(callback);
}

void GameClient::set_message_processing_break_callback(
    std::function<bool()> callback)
{
    message_processing_break_callback_ = std::move(callback);
}

void GameClient::send_input(const InputState& input, std::uint32_t tick)
{
    update_transport_connection_state();
    maybe_send_hello_if_needed();
    note_outbound_activity();
    transport_.send_input(server_peer_id_,
                          std::make_shared<InputState>(input),
                          tick);
}

void GameClient::send_client_ready()
{
    update_transport_connection_state();
    maybe_send_hello_if_needed();
    ClientReadyMessage message;
    message.last_applied_tick =
        baseline_.has_value() ? baseline_->tick_count : last_seen_server_tick_;
    transport_.send_client_ready(
        server_peer_id_,
        std::make_shared<ClientReadyMessage>(message));
    note_outbound_activity();
    client_ready_sent_ = true;
    ++client_ready_count_;
}

void GameClient::send_keyframe_request(std::uint32_t last_seen_tick)
{
    update_transport_connection_state();
    maybe_send_hello_if_needed();
    KeyframeRequestMessage message;
    message.last_seen_tick =
        last_seen_tick != 0 ? last_seen_tick : last_seen_server_tick_;
    transport_.send_keyframe_request(
        server_peer_id_,
        std::make_shared<KeyframeRequestMessage>(message));
    note_outbound_activity();
    waiting_for_keyframe_ = true;
    ++keyframe_request_count_;
}

void GameClient::send_exit_prompt_response(bool accepted)
{
    update_transport_connection_state();
    maybe_send_hello_if_needed();
    ExitPromptResponseMessage message;
    message.accepted = accepted;
    transport_.send_exit_prompt_response(
        server_peer_id_,
        std::make_shared<ExitPromptResponseMessage>(message));
    note_outbound_activity();
}

void GameClient::request_level_abort()
{
    update_transport_connection_state();
    maybe_send_hello_if_needed();
    ExitPromptResponseMessage message;
    message.accepted = true;
    message.abort_request = true;
    transport_.send_exit_prompt_response(
        server_peer_id_,
        std::make_shared<ExitPromptResponseMessage>(message));
    note_outbound_activity();
}

void GameClient::send_pause_request()
{
    update_transport_connection_state();
    maybe_send_hello_if_needed();
    transport_.send_pause_broadcast(
        server_peer_id_,
        std::make_shared<PauseBroadcastMessage>());
    note_outbound_activity();
}

void GameClient::send_pause_response()
{
    update_transport_connection_state();
    maybe_send_hello_if_needed();
    PauseResponseMessage message;
    message.resume = true;
    transport_.send_pause_response(
        server_peer_id_,
        std::make_shared<PauseResponseMessage>(message));
    note_outbound_activity();
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

    update_transport_connection_state();
    maybe_send_hello_if_needed();
    SnapshotHashCheckMessage message;
    message.tick = baseline_.has_value() ? baseline_->tick_count : last_seen_server_tick_;
    message.snapshot_hash = compute_local_snapshot_hash();
    transport_.send_snapshot_hash_check(
        server_peer_id_,
        std::make_shared<SnapshotHashCheckMessage>(message));
    note_outbound_activity();
    ++snapshot_hash_check_count_;
}

void GameClient::update_transport_connection_state()
{
    const std::vector<PeerId> peers = transport_.connected_peers();
    const bool connected = std::find(peers.begin(), peers.end(), server_peer_id_) !=
        peers.end();
    if (connected)
    {
        transport_ever_connected_ = true;
        transport_disconnect_time_.reset();
        connection_lost_notified_ = false;
    }
    else if (transport_connected_)
    {
        waiting_for_keyframe_ = true;
        hello_sent_for_connection_ = false;
        hello_acknowledged_ = false;
        transport_disconnect_time_ = InterpolationClock::now();
    }
    else if (transport_ever_connected_ && !transport_disconnect_time_.has_value())
    {
        transport_disconnect_time_ = InterpolationClock::now();
    }
    transport_connected_ = connected;
}

void GameClient::maybe_send_hello_if_needed()
{
    if (!transport_connected_ || hello_sent_for_connection_)
        return;

    HelloMessage message;
    message.session_token = session_token_;
    transport_.send_hello(
        server_peer_id_,
        std::make_shared<HelloMessage>(message));
    note_outbound_activity();
    hello_sent_for_connection_ = true;
}

void GameClient::maybe_send_heartbeat_if_needed()
{
    if (!transport_connected_)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_client", "heartbeat_disconnected"));
        return;
    }

    const auto now = InterpolationClock::now();
    const auto last_activity =
        last_outbound_activity_time_.value_or(InterpolationClock::time_point{});
    if (last_outbound_activity_time_.has_value() &&
        now - last_activity < kHeartbeatInterval)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_client", "heartbeat_suppressed_by_activity"));
        return;
    }

    HeartbeatMessage message;
    transport_.send_heartbeat(
        server_peer_id_,
        std::make_shared<HeartbeatMessage>(message));
    note_outbound_activity();
    og::runtime::RuntimeTraceRecord trace =
        og::runtime::make_runtime_trace_record(
            "game_client", "heartbeat_sent");
    trace.snapshot_kind = "heartbeat";
    og::runtime::emit_runtime_trace(std::move(trace));
}

void GameClient::maybe_notify_connection_lost()
{
    if (transport_connected_ || !transport_ever_connected_ ||
        connection_lost_notified_ || !transport_disconnect_time_.has_value())
    {
        return;
    }

    const auto timeout =
        std::chrono::milliseconds(CLIENT_CONNECTION_LOST_TIMEOUT_MS);
    if (InterpolationClock::now() - *transport_disconnect_time_ < timeout)
        return;

    connection_lost_notified_ = true;
    if (connection_lost_callback_)
        connection_lost_callback_();
}

void GameClient::note_keyframe_apply_result(bool applied_cleanly)
{
    if (applied_cleanly)
    {
        rejected_keyframe_strikes_ = 0;
        return;
    }

    ++rejected_keyframe_strikes_;
    LogError("game_client_rejected_keyframe strikes={} limit={}\n",
             rejected_keyframe_strikes_, kMaxRejectedKeyframeStrikes);
    if (fatal_desync_ ||
        rejected_keyframe_strikes_ < kMaxRejectedKeyframeStrikes)
    {
        return;
    }

    // Bounded loud failure (no in-protocol resync exists for a map-level
    // divergence — that would need an InitialSetup re-handshake): mark the
    // session dead and surface it through the connection-lost seam every
    // embedder already ends the session on.
    fatal_desync_ = true;
    LogError(
        "game_client_fatal_desync: {} consecutive full-grid snapshots "
        "rejected — ending the session (state divergence is unrecoverable)\n",
        rejected_keyframe_strikes_);
    TRACE("net", "client_fatal_desync strikes=%u",
          static_cast<unsigned>(rejected_keyframe_strikes_));
    if (!connection_lost_notified_)
    {
        connection_lost_notified_ = true;
        if (connection_lost_callback_)
            connection_lost_callback_();
    }
}

void GameClient::note_outbound_activity()
{
    last_outbound_activity_time_ = InterpolationClock::now();
}

void GameClient::maybe_send_client_ready()
{
    if (!client_ready_sent_ && initial_setup_.has_value() && baseline_.has_value())
        send_client_ready();
}

void GameClient::maybe_send_snapshot_hash_check(bool force)
{
    if (!baseline_.has_value())
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_client", "snapshot_hash_without_baseline"));
        return;
    }

    if (!force &&
        (baseline_->tick_count % KEYFRAME_INTERVAL_TICKS) != 0)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_client", "snapshot_hash_not_due"));
        return;
    }

    send_snapshot_hash_check();
    og::runtime::RuntimeTraceRecord trace =
        og::runtime::make_runtime_trace_record(
            "game_client",
            force ? "snapshot_hash_forced" : "snapshot_hash_periodic");
    trace.snapshot_kind = "snapshot_hash";
    if (baseline_.has_value())
        trace.tick = baseline_->tick_count;
    og::runtime::emit_runtime_trace(std::move(trace));
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

void GameClient::reset_render_interpolation()
{
    render_interpolation_.clear();
    last_snapshot_receive_time_.reset();
}

RenderInterpolationPosition GameClient::interpolate_position(
    const EntityInterpolationState& state,
    float alpha) noexcept
{
    const float clamped_alpha = clamp_alpha(alpha);
    RenderInterpolationPosition position;
    position.worldx = lerp(state.prev.worldx, state.curr.worldx, clamped_alpha);
    position.worldy = lerp(state.prev.worldy, state.curr.worldy, clamped_alpha);
    position.xpos = lerp(state.prev.xpos, state.curr.xpos, clamped_alpha);
    position.ypos = lerp(state.prev.ypos, state.curr.ypos, clamped_alpha);
    return position;
}

void GameClient::update_render_interpolation(const WorldSnapshot& snapshot,
                                             bool reset_history,
                                             float prior_alpha)
{
    std::unordered_map<std::uint32_t, EntityInterpolationState> next_state;
    next_state.reserve(snapshot.oblist.size() + snapshot.fxlist.size() +
                       snapshot.weaplist.size());

    const auto capture_position =
        [](const EntitySnapshot& entity_snapshot) -> RenderInterpolationPosition {
        return {
            .worldx = entity_snapshot.worldx,
            .worldy = entity_snapshot.worldy,
            // Render-time anchors should follow the authoritative float
            // position, not the snapshot-snapped integer grid coordinates.
            .xpos = entity_snapshot.worldx,
            .ypos = entity_snapshot.worldy,
        };
    };

    const auto update_entities =
        [this, &next_state, reset_history, prior_alpha, &capture_position](
            const std::vector<EntitySnapshot>& entities) {
            for (const EntitySnapshot& entity_snapshot : entities)
            {
                if (entity_snapshot.entity_id == 0u || entity_snapshot.dead != 0)
                    continue;

                EntityInterpolationState state;
                state.curr = capture_position(entity_snapshot);

                if (!reset_history)
                {
                    const auto existing =
                        render_interpolation_.find(entity_snapshot.entity_id);
                    state.prev = existing != render_interpolation_.end()
                        ? interpolate_position(existing->second, prior_alpha)
                        : state.curr;
                }
                else
                {
                    state.prev = state.curr;
                }

                next_state[entity_snapshot.entity_id] = state;
            }
        };

    update_entities(snapshot.oblist);
    update_entities(snapshot.fxlist);
    update_entities(snapshot.weaplist);

    render_interpolation_ = std::move(next_state);
    const float preserved_alpha = clamp_alpha(prior_alpha);
    const float interval_ms = og::core::rounded_render_tick_interval_ms(
        snapshot.timer_wait,
        last_render_speed_factor_);
    const auto now = InterpolationClock::now();
    if (interval_ms > 0.0f && preserved_alpha > 0.0f)
    {
        last_snapshot_receive_time_ =
            now -
            std::chrono::duration_cast<InterpolationClock::duration>(
                std::chrono::duration<float, std::milli>(
                    preserved_alpha * interval_ms));
    }
    else
    {
        last_snapshot_receive_time_ = now;
    }
}

void GameClient::apply_initial_setup(const InitialSetupMessage& message)
{
    // A transition is any setup the server marked ready-resetting: a moved
    // level id/scenario, or (v13) a bumped setup_generation — the same level
    // reloaded fresh (quit-mission withdraw reloads the CURRENT level;
    // prepare_clients_for_loaded_level reset our client_ready either way, so
    // the platform callbacks must re-ready or the launch gate never opens).
    // Mid-level resends (control mapping, reconnect catch-up) keep the
    // generation and stay non-transitional.
    const bool is_level_transition =
        baseline_.has_value() &&
        (!initial_setup_.has_value() ||
         initial_setup_->level_id != message.level_id ||
         initial_setup_->current_scenario != message.current_scenario ||
         initial_setup_->setup_generation != message.setup_generation);
    initial_setup_ = message;
    controlled_entity_ids_ = message.controlled_entity_ids;
    initial_setup_guys_.clear();
    for (const auto& guy : message.guys)
        initial_setup_guys_[guy.guy_id] = guy;

    if (world_ != nullptr)
        apply_initial_setup_to_world(*world_, message);

    baseline_.reset();
    reset_render_interpolation();
    last_seen_server_tick_ = 0;
    waiting_for_keyframe_ = false;
    client_ready_sent_ = false;
    // A (re-)setup legitimately swaps the world/grid: stale strikes from the
    // previous level must not count against the new one. fatal_desync_ stays
    // latched — the session already surfaced its error.
    rejected_keyframe_strikes_ = 0;
    has_sim_event_sequence_ = false;
    has_game_flow_event_sequence_ = false;
    last_exit_prompt_.reset();
    last_pause_broadcast_.reset();
    notify_initial_setup(message, is_level_transition);
    notify_control_mapping_changed();
}

void GameClient::apply_full_snapshot(const WorldSnapshot& snapshot,
                                     float prior_alpha)
{
    const bool reset_history = !baseline_.has_value() || waiting_for_keyframe_;
    baseline_ = snapshot;
    update_render_interpolation(
        *baseline_, reset_history, reset_history ? 1.0f : prior_alpha);
    if (world_ != nullptr)
    {
        const bool carried_full_grid = baseline_->grid_full_resend;
        const bool applied_cleanly = apply_snapshot(*world_, *baseline_);
        clear_transport_only_world_state(*world_);
        if (carried_full_grid)
            note_keyframe_apply_result(applied_cleanly);
    }
    clear_transport_only_snapshot_state(*baseline_);
    last_seen_server_tick_ = baseline_->tick_count;
    notify_control_mapping_changed();
    notify_palette_sync(baseline_->current_palette_id);
    waiting_for_keyframe_ = false;
    // When a keyframe establishes a fresh baseline (initial sync or a level
    // transition), the server has reset its client_ready and expects a fresh
    // confirmation before it streams deltas. Re-confirm even if we already sent
    // ready for this setup — otherwise a level-transition keyframe deadlocks: the
    // transition display callback sent client_ready before the keyframe, so the
    // post-keyframe maybe_send_client_ready() would no-op and the server would
    // never resume sending — the next level freezes ("no one can move").
    if (reset_history)
        client_ready_sent_ = false;
    maybe_send_client_ready();
    maybe_send_snapshot_hash_check(true);
}

void GameClient::apply_delta_snapshot(const WorldSnapshot& snapshot,
                                      float prior_alpha)
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
    update_render_interpolation(*baseline_, false, prior_alpha);
    if (world_ != nullptr)
    {
        // A delta can carry a full-grid resend too (dirty-tile overflow);
        // its rejection is the same unrecoverable shape as a keyframe's.
        const bool carried_full_grid = baseline_->grid_full_resend;
        const bool applied_cleanly = apply_snapshot(*world_, *baseline_);
        clear_transport_only_world_state(*world_);
        if (carried_full_grid)
            note_keyframe_apply_result(applied_cleanly);
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

void GameClient::poll_messages_impl(float first_snapshot_prior_alpha,
                                    int max_messages)
{
    og::runtime::RuntimeTraceRecord start_trace =
        og::runtime::make_runtime_trace_record(
            "game_client", "poll_messages_begin");
    start_trace.interpolation_alpha = clamp_alpha(first_snapshot_prior_alpha);
    og::runtime::emit_runtime_trace(std::move(start_trace));

    update_transport_connection_state();
    maybe_send_hello_if_needed();
    maybe_send_heartbeat_if_needed();

    ClientPollResult poll_result = poll_client_messages(transport_);
    if (poll_result.malformed_server_message)
    {
        og::runtime::emit_runtime_trace(
            og::runtime::make_runtime_trace_record(
                "game_client", "poll_messages_malformed"));
        LogError("game_client_malformed_message peer={}\n", server_peer_id_);
        transport_.disconnect(server_peer_id_);
        last_polled_messages_.clear();
        pending_inbound_messages_.clear();
        sim_event_batches_.clear();
        game_flow_event_batches_.clear();
        messages_drained_last_call_ = 0;
        update_transport_connection_state();
        return;
    }

    update_transport_connection_state();
    maybe_send_hello_if_needed();
    maybe_send_heartbeat_if_needed();

    for (auto& message : poll_result.messages)
        pending_inbound_messages_.push_back(std::move(message));
    poll_result.messages.clear();

    const std::size_t available = pending_inbound_messages_.size();
    const std::size_t budget =
        max_messages <= 0 ? 0u : static_cast<std::size_t>(max_messages);
    const std::size_t to_drain = std::min(available, budget);

    og::runtime::RuntimeTraceRecord polled_trace =
        og::runtime::make_runtime_trace_record(
            "game_client",
            to_drain == 0u ? "poll_messages_empty"
                           : "poll_messages_received");
    og::runtime::emit_runtime_trace(std::move(polled_trace));

    last_polled_messages_.clear();
    last_polled_messages_.reserve(to_drain);
    for (std::size_t i = 0; i < to_drain; ++i)
    {
        last_polled_messages_.push_back(
            std::move(pending_inbound_messages_.front()));
        pending_inbound_messages_.pop_front();
    }
    messages_drained_last_call_ = static_cast<int>(to_drain);
    sim_event_batches_.clear();
    game_flow_event_batches_.clear();

    const auto has_remaining_transition_initial_setup =
        [this](std::size_t next_index) {
            for (std::size_t index = next_index;
                 index < last_polled_messages_.size();
                 ++index)
            {
                const TypedReceivedMessage& queued = last_polled_messages_[index];
                if (queued.peer_id == server_peer_id_ &&
                    queued.kind == TypedReceivedMessageKind::InitialSetup &&
                    queued.initial_setup)
                {
                    return true;
                }
            }
            return false;
    };

    float next_snapshot_prior_alpha = clamp_alpha(first_snapshot_prior_alpha);
    for (std::size_t index = 0; index < last_polled_messages_.size(); ++index)
    {
        const auto& message = last_polled_messages_[index];
        if (message.peer_id != server_peer_id_)
            continue;

        switch (message.kind)
        {
        case TypedReceivedMessageKind::Snapshot:
            if (message.snapshot)
            {
                apply_full_snapshot(*message.snapshot,
                                    next_snapshot_prior_alpha);
                next_snapshot_prior_alpha = 0.0f;
            }
            break;

        case TypedReceivedMessageKind::DeltaSnapshot:
            if (message.snapshot)
            {
                apply_delta_snapshot(*message.snapshot,
                                     next_snapshot_prior_alpha);
                next_snapshot_prior_alpha = 0.0f;
            }
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

        case TypedReceivedMessageKind::LobbyMessage:
        case TypedReceivedMessageKind::LobbyState:
            break;

        case TypedReceivedMessageKind::InitialSetup:
            if (message.initial_setup)
                apply_initial_setup(*message.initial_setup);
            break;

        case TypedReceivedMessageKind::Hello:
            if (message.hello)
            {
                session_token_ = message.hello->session_token;
                hello_acknowledged_ = !is_zero_session_token(session_token_);
            }
            break;

        case TypedReceivedMessageKind::ExitPromptBroadcast:
            if (message.exit_prompt_broadcast)
            {
                last_exit_prompt_ = *message.exit_prompt_broadcast;
                notify_exit_prompt(*message.exit_prompt_broadcast);
            }
            break;

        case TypedReceivedMessageKind::Heartbeat:
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
        // Pack transfers complete during the lobby phase; the gameplay
        // client ignores stragglers.
        case TypedReceivedMessageKind::PackManifest:
        case TypedReceivedMessageKind::PackRequest:
        case TypedReceivedMessageKind::PackFileChunk:
        case TypedReceivedMessageKind::PackTransferDone:
        // Staged-lobby broadcasts (v13) belong to the lobby pollers; the
        // gameplay client ignores a straggling pair.
        case TypedReceivedMessageKind::StagedMatchSetup:
        case TypedReceivedMessageKind::StagedMatchKeyframe:
        case TypedReceivedMessageKind::Malformed:
            break;
        }

        if (message_processing_break_callback_ &&
            message_processing_break_callback_() &&
            !has_remaining_transition_initial_setup(index + 1))
        {
            break;
        }
    }

    maybe_notify_connection_lost();
    og::runtime::emit_runtime_trace(
        og::runtime::make_runtime_trace_record(
            "game_client", "poll_messages_end"));
}

} // namespace og::sim
