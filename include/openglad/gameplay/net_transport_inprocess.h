#pragma once

#include <openglad/gameplay/net_transport.h>

#include <memory>
#include <mutex>
#include <unordered_map>

#ifndef OPENGLAD_VALIDATE_SERIALIZATION
#define OPENGLAD_VALIDATE_SERIALIZATION 0
#endif

namespace og::sim {

inline constexpr bool kInProcessTransportValidateSerializationDefault =
    OPENGLAD_VALIDATE_SERIALIZATION != 0;

class InProcessTransport final : public ITransport,
                                 public std::enable_shared_from_this<InProcessTransport>
{
public:
    struct Options {
        bool validate_serialization =
            kInProcessTransportValidateSerializationDefault;
    };

    struct LinkedPair {
        std::shared_ptr<InProcessTransport> server;
        std::shared_ptr<InProcessTransport> client;
        PeerId peer_id = 0;
    };

    static std::shared_ptr<InProcessTransport> create_server();
    static std::shared_ptr<InProcessTransport> create_server(
        const Options& options);
    static LinkedPair create_linked_pair();
    static LinkedPair create_linked_pair(const Options& options);

    [[nodiscard]] std::shared_ptr<InProcessTransport> create_client_transport();
    [[nodiscard]] PeerId local_peer_id() const noexcept { return local_peer_id_; }
    [[nodiscard]] bool validation_enabled() const noexcept
    {
        return validate_serialization_;
    }

    using ITransport::send_input;

    [[nodiscard]] bool supports_typed_messages() const noexcept override;

    void send(PeerId peer_id, const std::uint8_t* data, std::size_t len) override;
    void send_snapshot(PeerId peer_id,
                       std::shared_ptr<WorldSnapshot> snapshot) override;
    void send_delta_snapshot(PeerId peer_id,
                             std::shared_ptr<WorldSnapshot> snapshot) override;
    void send_input(PeerId peer_id,
                    std::shared_ptr<InputState> input,
                    std::uint32_t tick) override;
    void send_sim_event_batch(PeerId peer_id,
                              std::shared_ptr<SimEventBatch> batch) override;
    void send_game_flow_event_batch(PeerId peer_id,
                                    std::shared_ptr<SimEventBatch> batch) override;
    void send_initial_setup(PeerId peer_id,
                            std::shared_ptr<InitialSetupMessage> message) override;
    void send_client_ready(PeerId peer_id,
                           std::shared_ptr<ClientReadyMessage> message) override;
    void send_keyframe_request(
        PeerId peer_id,
        std::shared_ptr<KeyframeRequestMessage> message) override;
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
    InProcessTransport(bool is_server_side,
                       PeerId local_peer_id,
                       const Options& options);

    void attach_peer(PeerId peer_id,
                     const std::shared_ptr<InProcessTransport>& peer_transport);
    void detach_peer(PeerId peer_id);
    [[nodiscard]] std::shared_ptr<InProcessTransport> resolve_peer(
        PeerId peer_id) const;
    void enqueue_raw(PeerId peer_id, std::vector<std::uint8_t> bytes);
    void enqueue_typed(TypedReceivedMessage message);

    const bool is_server_side_ = false;
    const PeerId local_peer_id_ = 0;
    const bool validate_serialization_ = false;

    mutable std::mutex mutex_;
    bool accepting_connections_ = false;
    PeerId next_peer_id_ = 1;
    std::unordered_map<PeerId, std::weak_ptr<InProcessTransport>> peers_;
    std::vector<ReceivedMessage> raw_messages_;
    std::vector<TypedReceivedMessage> typed_messages_;
};

} // namespace og::sim
