#pragma once

#include <openglad/gameplay/net_transport.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace og::sim {

class WebSocketServerTransport final : public ITransport
{
public:
    struct Options {
        std::string host = "0.0.0.0";
        int backlog = 5;
        std::size_t max_connections = static_cast<std::size_t>(MAX_PLAYERS);
    };

    // IXWebSocket invokes callbacks on background I/O threads. Those callbacks
    // only enqueue connection/message/disconnection events here; all peer state
    // updates happen later on the game thread from poll(). A send to a peer
    // whose disconnect is still deferred is treated as a no-op and queues a
    // disconnect for the next poll instead of throwing from the game thread.
    explicit WebSocketServerTransport(int port);
    WebSocketServerTransport(int port, Options options);
    ~WebSocketServerTransport() override;

    WebSocketServerTransport(const WebSocketServerTransport&) = delete;
    WebSocketServerTransport& operator=(const WebSocketServerTransport&) = delete;
    WebSocketServerTransport(WebSocketServerTransport&&) = delete;
    WebSocketServerTransport& operator=(WebSocketServerTransport&&) = delete;

    void send(PeerId peer_id,
              const std::uint8_t* data,
              std::size_t len) override;
    [[nodiscard]] std::vector<ReceivedMessage> poll() override;
    void accept_connections() override;
    void disconnect(PeerId peer_id) override;
    [[nodiscard]] std::vector<PeerId> connected_peers() const override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace og::sim
