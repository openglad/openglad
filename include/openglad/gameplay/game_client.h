#pragma once

#include <openglad/gameplay/net_transport.h>
#include <openglad/gameplay/world_snapshot.h>

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

class GameWorld;

namespace og::sim {

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
    void send_pause_request();
    void send_pause_response();
    void send_snapshot_hash_check();
    void poll_messages();
    void set_control_mapping_callback(
        std::function<void(const std::array<std::uint32_t, MAX_PLAYERS>&,
                           GameWorld*)> callback);
    void set_sim_event_batch_callback(
        std::function<void(const SimEventBatch&)> callback);
    void set_game_flow_event_batch_callback(
        std::function<void(const SimEventBatch&)> callback);
    void set_exit_prompt_callback(
        std::function<void(const ExitPromptBroadcastMessage&)> callback);
    void set_pause_broadcast_callback(
        std::function<void(const PauseBroadcastMessage&)> callback);

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

private:
    void apply_full_snapshot(const WorldSnapshot& snapshot);
    void apply_delta_snapshot(const WorldSnapshot& snapshot);
    void apply_initial_setup(const InitialSetupMessage& message);
    void note_event_batch_gap(std::uint32_t expected,
                              std::uint32_t actual,
                              const char* label) const;
    void notify_control_mapping_changed();
    void notify_sim_event_batch(const SimEventBatch& batch);
    void notify_game_flow_event_batch(const SimEventBatch& batch);
    void notify_exit_prompt(const ExitPromptBroadcastMessage& message);
    void notify_pause_broadcast(const PauseBroadcastMessage& message);
    std::uint32_t compute_local_snapshot_hash() const;
    void maybe_send_client_ready();
    void maybe_send_snapshot_hash_check(bool force = false);

    ITransport& transport_;
    PeerId server_peer_id_ = 0;
    GameWorld* world_ = nullptr;
    std::optional<WorldSnapshot> baseline_ = std::nullopt;
    std::optional<InitialSetupMessage> initial_setup_ = std::nullopt;
    std::vector<TypedReceivedMessage> last_polled_messages_;
    std::vector<SimEventBatch> sim_event_batches_;
    std::vector<SimEventBatch> game_flow_event_batches_;
    std::array<std::uint32_t, MAX_PLAYERS> controlled_entity_ids_ = {};
    std::unordered_map<std::int32_t, InitialSetupGuyData> initial_setup_guys_;
    std::optional<ExitPromptBroadcastMessage> last_exit_prompt_ = std::nullopt;
    std::optional<PauseBroadcastMessage> last_pause_broadcast_ = std::nullopt;
    std::uint32_t last_seen_server_tick_ = 0;
    std::uint32_t last_sim_event_sequence_ = 0;
    std::uint32_t last_game_flow_event_sequence_ = 0;
    bool has_sim_event_sequence_ = false;
    bool has_game_flow_event_sequence_ = false;
    bool waiting_for_keyframe_ = false;
    bool client_ready_sent_ = false;
    std::uint32_t client_ready_count_ = 0;
    std::uint32_t keyframe_request_count_ = 0;
    std::uint32_t snapshot_hash_check_count_ = 0;
    std::function<void(const std::array<std::uint32_t, MAX_PLAYERS>&,
                       GameWorld*)> control_mapping_callback_;
    std::function<void(const SimEventBatch&)> sim_event_batch_callback_;
    std::function<void(const SimEventBatch&)> game_flow_event_batch_callback_;
    std::function<void(const ExitPromptBroadcastMessage&)> exit_prompt_callback_;
    std::function<void(const PauseBroadcastMessage&)> pause_broadcast_callback_;
};

} // namespace og::sim
