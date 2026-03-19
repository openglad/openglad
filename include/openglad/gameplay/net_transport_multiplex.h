#pragma once

#include <openglad/gameplay/net_transport.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace og::sim {

class MultiplexTransport final : public ITransport
{
public:
    explicit MultiplexTransport(std::vector<std::shared_ptr<ITransport>> transports);

    [[nodiscard]] bool supports_typed_messages() const noexcept override;
    void send(PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override;
    void send_snapshot(PeerId peer_id,
                       std::shared_ptr<WorldSnapshot> snapshot) override;
    void send_delta_snapshot(PeerId peer_id,
                             std::shared_ptr<WorldSnapshot> snapshot) override;
    void send_input(PeerId peer_id,
                    std::shared_ptr<InputState> input,
                    std::uint32_t tick) override;
    void send_sim_event_batch(PeerId peer_id,
                              std::shared_ptr<SimEventBatch> batch) override;
    void send_game_flow_event_batch(
        PeerId peer_id,
        std::shared_ptr<SimEventBatch> batch) override;
    void send_lobby_message(PeerId peer_id,
                            std::shared_ptr<LobbyMessage> message) override;
    void send_lobby_state(PeerId peer_id,
                          std::shared_ptr<LobbyState> state) override;
    void send_initial_setup(
        PeerId peer_id,
        std::shared_ptr<InitialSetupMessage> message) override;
    void send_hello(
        PeerId peer_id,
        std::shared_ptr<HelloMessage> message) override;
    void send_client_ready(
        PeerId peer_id,
        std::shared_ptr<ClientReadyMessage> message) override;
    void send_keyframe_request(
        PeerId peer_id,
        std::shared_ptr<KeyframeRequestMessage> message) override;
    void send_heartbeat(
        PeerId peer_id,
        std::shared_ptr<HeartbeatMessage> message) override;
    void send_exit_prompt_broadcast(
        PeerId peer_id,
        std::shared_ptr<ExitPromptBroadcastMessage> message) override;
    void send_exit_prompt_response(
        PeerId peer_id,
        std::shared_ptr<ExitPromptResponseMessage> message) override;
    void send_pause_broadcast(
        PeerId peer_id,
        std::shared_ptr<PauseBroadcastMessage> message) override;
    void send_pause_response(
        PeerId peer_id,
        std::shared_ptr<PauseResponseMessage> message) override;
    void send_control_change(
        PeerId peer_id,
        std::shared_ptr<ControlChangeMessage> message) override;
    void send_snapshot_hash_check(
        PeerId peer_id,
        std::shared_ptr<SnapshotHashCheckMessage> message) override;
    [[nodiscard]] std::vector<ReceivedMessage> poll() override;
    [[nodiscard]] std::vector<TypedReceivedMessage> poll_typed() override;
    void accept_connections() override;
    void disconnect(PeerId peer_id) override;
    [[nodiscard]] std::vector<PeerId> connected_peers() const override;

private:
    struct EndpointState {
        std::shared_ptr<ITransport> transport;
        std::unordered_map<PeerId, PeerId> native_to_public;
        std::unordered_map<PeerId, PeerId> public_to_native;
    };

    [[nodiscard]] PeerId ensure_public_peer(EndpointState& endpoint,
                                            PeerId native_peer_id) const;
    void sync_endpoint_peers(EndpointState& endpoint) const;
    [[nodiscard]] EndpointState* find_endpoint_for_public_peer(
        PeerId peer_id) noexcept;

    mutable std::vector<EndpointState> endpoints_;
    mutable PeerId next_public_peer_id_ = 1;
};

} // namespace og::sim
