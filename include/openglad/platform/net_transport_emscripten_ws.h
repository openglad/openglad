#pragma once

#include <openglad/gameplay/net_transport.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace og::sim {

class EmscriptenWebSocketTransport final : public ITransport
{
public:
    struct Options {
        PeerId remote_peer_id = 1;
        std::string protocols;
    };

    // Browser WebSocket callbacks are event-loop driven and only enqueue
    // connection/message/disconnection events here. The single remote peer
    // state is updated later on the game thread from poll(). A send while
    // disconnected is treated as a no-op.
    explicit EmscriptenWebSocketTransport(std::string url);
    EmscriptenWebSocketTransport(std::string url, Options options);
    ~EmscriptenWebSocketTransport() override;

    EmscriptenWebSocketTransport(const EmscriptenWebSocketTransport&) = delete;
    EmscriptenWebSocketTransport& operator=(
        const EmscriptenWebSocketTransport&) = delete;
    EmscriptenWebSocketTransport(EmscriptenWebSocketTransport&&) = delete;
    EmscriptenWebSocketTransport& operator=(
        EmscriptenWebSocketTransport&&) = delete;

    void send(PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override;
    [[nodiscard]] std::vector<ReceivedMessage> poll() override;
    void accept_connections() override;
    void disconnect(PeerId peer_id) override;
    [[nodiscard]] std::vector<PeerId> connected_peers() const override;
    [[nodiscard]] TransportLinkState link_state() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace og::sim
