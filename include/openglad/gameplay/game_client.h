#pragma once

#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/world_snapshot.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

class GameWorld;

namespace og::sim {

// Consecutive full-grid (keyframe-shaped) snapshots the client may reject
// before it declares the session unrecoverably desynced: the server keeps
// force-resending keyframes on every hash mismatch, so a persistent grid-size
// rejection would otherwise rubber-band forever (the display walks one map
// while the authority simulates another). Three strikes covers any transient
// in-flight overlap; a genuine map mismatch rejects every one.
inline constexpr std::uint32_t kMaxRejectedKeyframeStrikes = 3;

struct RenderInterpolationPosition {
    float worldx = 0.0f;
    float worldy = 0.0f;
    float xpos = 0.0f;
    float ypos = 0.0f;
};

class GameClient
{
public:
    explicit GameClient(ITransport& transport,
                        PeerId server_peer_id,
                        GameWorld* world = nullptr);

    void send_input(const InputState& input, std::uint32_t tick);
    void send_client_ready();
    void send_keyframe_request(std::uint32_t last_seen_tick = 0);
    void send_exit_prompt_response(bool accepted);
    // Ask the server to withdraw ALL players to the current level (return every
    // peer to the team-build menu). Used when this peer chooses "Quit this
    // mission" — a deliberate party-wide retreat, distinct from a disconnect
    // (which only converts the leaving player's character to AI).
    void request_level_abort();
    void send_pause_request();
    void send_pause_response();
    void send_snapshot_hash_check();
    void poll_messages();
    void poll_messages(float current_render_alpha);
    void poll_messages(float current_render_alpha, int max_messages);
    void set_control_mapping_callback(
        std::function<void(const std::array<std::uint32_t, MAX_PLAYERS>&,
                           GameWorld*)> callback);
    void set_initial_setup_callback(
        std::function<void(const InitialSetupMessage&, bool is_level_transition)>
            callback);
    void set_sim_event_batch_callback(
        std::function<void(const SimEventBatch&)> callback);
    void set_game_flow_event_batch_callback(
        std::function<void(const SimEventBatch&)> callback);
    void set_exit_prompt_callback(
        std::function<void(const ExitPromptBroadcastMessage&)> callback);
    void set_pause_broadcast_callback(
        std::function<void(const PauseBroadcastMessage&)> callback);
    void set_palette_sync_callback(
        std::function<void(std::uint8_t palette_id)> callback);
    void set_connection_lost_callback(std::function<void()> callback);
    void set_message_processing_break_callback(
        std::function<bool()> callback);

    [[nodiscard]] const std::optional<WorldSnapshot>& baseline() const noexcept
    {
        return baseline_;
    }

    [[nodiscard]] const std::vector<TypedReceivedMessage>&
    last_polled_messages() const noexcept
    {
        return last_polled_messages_;
    }

    [[nodiscard]] const std::vector<SimEventBatch>&
    sim_event_batches() const noexcept
    {
        return sim_event_batches_;
    }

    [[nodiscard]] const std::vector<SimEventBatch>&
    game_flow_event_batches() const noexcept
    {
        return game_flow_event_batches_;
    }

    [[nodiscard]] const std::optional<InitialSetupMessage>&
    initial_setup() const noexcept
    {
        return initial_setup_;
    }

    [[nodiscard]] const std::array<std::uint32_t, MAX_PLAYERS>&
    controlled_entity_ids() const noexcept
    {
        return controlled_entity_ids_;
    }

    [[nodiscard]] const std::optional<ExitPromptBroadcastMessage>&
    last_exit_prompt() const noexcept
    {
        return last_exit_prompt_;
    }

    [[nodiscard]] const std::optional<PauseBroadcastMessage>&
    last_pause_broadcast() const noexcept
    {
        return last_pause_broadcast_;
    }

    [[nodiscard]] bool waiting_for_keyframe() const noexcept
    {
        return waiting_for_keyframe_;
    }

    [[nodiscard]] std::uint32_t last_seen_server_tick() const noexcept
    {
        return last_seen_server_tick_;
    }

    [[nodiscard]] std::uint32_t client_ready_count() const noexcept
    {
        return client_ready_count_;
    }

    [[nodiscard]] std::uint32_t keyframe_request_count() const noexcept
    {
        return keyframe_request_count_;
    }

    [[nodiscard]] std::uint32_t snapshot_hash_check_count() const noexcept
    {
        return snapshot_hash_check_count_;
    }

    // True once kMaxRejectedKeyframeStrikes consecutive full-grid snapshots
    // were rejected (grid-size mismatch: this world runs a different map than
    // the server). The connection-lost callback has fired; the session is
    // over — there is no in-protocol resync from this state.
    [[nodiscard]] bool fatal_desync() const noexcept
    {
        return fatal_desync_;
    }

    [[nodiscard]] int messages_drained_last_call() const noexcept
    {
        return messages_drained_last_call_;
    }

    [[nodiscard]] std::size_t pending_inbound_message_count() const noexcept
    {
        return pending_inbound_messages_.size();
    }

    [[nodiscard]] const SessionToken& session_token() const noexcept
    {
        return session_token_;
    }

    [[nodiscard]] float render_interpolation_alpha(float speed_factor) const;
    [[nodiscard]] std::optional<RenderInterpolationPosition> render_position(
        std::uint32_t entity_id,
        float alpha) const;
    void testing_set_render_interpolation_elapsed_ms(float elapsed_ms);
    void testing_set_last_outbound_activity_elapsed_ms(float elapsed_ms);
    void testing_set_transport_disconnect_elapsed_ms(float elapsed_ms);

private:
    struct EntityInterpolationState {
        RenderInterpolationPosition prev = {};
        RenderInterpolationPosition curr = {};
    };

    using InterpolationClock = std::chrono::steady_clock;

    void apply_full_snapshot(const WorldSnapshot& snapshot, float prior_alpha);
    void apply_delta_snapshot(const WorldSnapshot& snapshot, float prior_alpha);
    void apply_initial_setup(const InitialSetupMessage& message);
    void poll_messages_impl(float first_snapshot_prior_alpha,
                            int max_messages);
    void reset_render_interpolation();
    void update_render_interpolation(const WorldSnapshot& snapshot,
                                     bool reset_history,
                                     float prior_alpha);
    [[nodiscard]] static RenderInterpolationPosition interpolate_position(
        const EntityInterpolationState& state,
        float alpha) noexcept;
    void note_event_batch_gap(std::uint32_t expected,
                              std::uint32_t actual,
                              const char* label) const;
    void notify_control_mapping_changed();
    void notify_initial_setup(const InitialSetupMessage& message,
                              bool is_level_transition);
    void notify_sim_event_batch(const SimEventBatch& batch);
    void notify_game_flow_event_batch(const SimEventBatch& batch);
    void notify_exit_prompt(const ExitPromptBroadcastMessage& message);
    void notify_pause_broadcast(const PauseBroadcastMessage& message);
    void notify_palette_sync(std::uint8_t palette_id);
    std::uint32_t compute_local_snapshot_hash() const;
    void update_transport_connection_state();
    void maybe_send_hello_if_needed();
    void maybe_send_heartbeat_if_needed();
    void maybe_notify_connection_lost();
    void note_keyframe_apply_result(bool applied_cleanly);
    void note_outbound_activity();
    void maybe_send_client_ready();
    void maybe_send_snapshot_hash_check(bool force = false);
    ITransport& transport_;
    PeerId server_peer_id_ = 0;
    GameWorld* world_ = nullptr;
    std::optional<WorldSnapshot> baseline_ = std::nullopt;
    std::optional<InitialSetupMessage> initial_setup_ = std::nullopt;
    std::vector<TypedReceivedMessage> last_polled_messages_;
    std::deque<TypedReceivedMessage> pending_inbound_messages_;
    int messages_drained_last_call_ = 0;
    std::vector<SimEventBatch> sim_event_batches_;
    std::vector<SimEventBatch> game_flow_event_batches_;
    std::array<std::uint32_t, MAX_PLAYERS> controlled_entity_ids_ = {};
    std::unordered_map<std::int32_t, InitialSetupGuyData> initial_setup_guys_;
    std::optional<ExitPromptBroadcastMessage> last_exit_prompt_ = std::nullopt;
    std::optional<PauseBroadcastMessage> last_pause_broadcast_ = std::nullopt;
    std::unordered_map<std::uint32_t, EntityInterpolationState>
        render_interpolation_;
    std::optional<InterpolationClock::time_point>
        last_snapshot_receive_time_ = std::nullopt;
    mutable float last_render_speed_factor_ = 1.0f;
    std::optional<InterpolationClock::time_point>
        last_outbound_activity_time_ = std::nullopt;
    std::optional<InterpolationClock::time_point>
        transport_disconnect_time_ = std::nullopt;
    std::uint32_t last_seen_server_tick_ = 0;
    std::uint32_t last_sim_event_sequence_ = 0;
    std::uint32_t last_game_flow_event_sequence_ = 0;
    bool has_sim_event_sequence_ = false;
    bool has_game_flow_event_sequence_ = false;
    SessionToken session_token_ = kZeroSessionToken;
    bool transport_connected_ = false;
    bool transport_ever_connected_ = false;
    bool hello_sent_for_connection_ = false;
    bool hello_acknowledged_ = false;
    bool connection_lost_notified_ = false;
    bool fatal_desync_ = false;
    std::uint32_t rejected_keyframe_strikes_ = 0;
    bool waiting_for_keyframe_ = false;
    bool client_ready_sent_ = false;
    std::uint32_t client_ready_count_ = 0;
    std::uint32_t keyframe_request_count_ = 0;
    std::uint32_t snapshot_hash_check_count_ = 0;
    std::function<void(const std::array<std::uint32_t, MAX_PLAYERS>&,
                       GameWorld*)> control_mapping_callback_;
    std::function<void(const InitialSetupMessage&, bool)> initial_setup_callback_;
    std::function<void(const SimEventBatch&)> sim_event_batch_callback_;
    std::function<void(const SimEventBatch&)> game_flow_event_batch_callback_;
    std::function<void(const ExitPromptBroadcastMessage&)> exit_prompt_callback_;
    std::function<void(const PauseBroadcastMessage&)> pause_broadcast_callback_;
    std::function<void(std::uint8_t)> palette_sync_callback_;
    std::function<void()> connection_lost_callback_;
    std::function<bool()> message_processing_break_callback_;
};

} // namespace og::sim
