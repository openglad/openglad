#pragma once

#include <openglad/gameplay/net_transport.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace og::sim {

class WebSocketClientTransport final : public ITransport
{
public:
    struct Options {
        PeerId remote_peer_id = 1;
        bool automatic_reconnection = true;
        std::uint32_t min_reconnect_wait_ms = 1;
        std::uint32_t max_reconnect_wait_ms = 10'000;
    };

    // IXWebSocket invokes callbacks on background I/O threads. Those callbacks
    // only enqueue connection/message/disconnection events here; the single
    // remote peer state is updated later on the game thread from poll(). A send
    // while disconnected or while IXWebSocket is reconnecting is treated as a
    // no-op instead of throwing from the game thread.
    explicit WebSocketClientTransport(std::string url);
    WebSocketClientTransport(std::string url, Options options);
    ~WebSocketClientTransport() override;

    WebSocketClientTransport(const WebSocketClientTransport&) = delete;
    WebSocketClientTransport& operator=(const WebSocketClientTransport&) = delete;
    WebSocketClientTransport(WebSocketClientTransport&&) = delete;
    WebSocketClientTransport& operator=(WebSocketClientTransport&&) = delete;

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
